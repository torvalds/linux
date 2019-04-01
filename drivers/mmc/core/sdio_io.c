/*
 *  linux/drivers/mmc/core/sdio_io.c
 *
 *  Copyright 2007-2008 Pierre Ossman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_func.h>

#include "sdio_ops.h"
#include "core.h"
#include "card.h"

/**
 *	sdio_claim_host - exclusively claim a bus for a certain SDIO function
 *	@func: SDIO function that will be accessed
 *
 *	Claim a bus for a set of operations. The SDIO function given
 *	is used to figure out which bus is relevant.
 */
void sdio_claim_host(struct sdio_func *func)
{
	if (WARN_ON(!func))
		return;

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
	if (WARN_ON(!func))
		return;

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

	if (!func)
		return -EINVAL;

	pr_debug("SDIO: Enabling device %s...\n", sdio_func_id(func));

	ret = mmc_io_rw_direct(func->card, 0, 0, SDIO_CCCR_IOEx, 0, &reg);
	if (ret)
		goto err;

	reg |= 1 << func->num;

	ret = mmc_io_rw_direct(func->card, 1, 0, SDIO_CCCR_IOEx, reg, NULL);
	if (ret)
		goto err;

	timeout = jiffies + msecs_to_jiffies(func->enable_timeout);

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

	if (!func)
		return -EINVAL;

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
 *	sdio_set_block_size - set the block size of an SDIO function
 *	@func: SDIO function to change
 *	@blksz: new block size or 0 to use the default.
 *
 *	The default block size is the largest supported by both the function
 *	and the host, with a maximum of 512 to ensure that arbitrarily sized
 *	data transfer use the optimal (least) number of commands.
 *
 *	A driver may call this to override the default block size set by the
 *	core. This can be used to set a block size greater than the maximum
 *	that reported by the card; it is the driver's responsibility to ensure
 *	it uses a value that the card supports.
 *
 *	Returns 0 on success, -EINVAL if the host does not support the
 *	requested block size, or -EIO (etc.) if one of the resultant FBR block
 *	size register writes failed.
 *
 */
int sdio_set_block_size(struct sdio_func *func, unsigned blksz)
{
	int ret;

	if (blksz > func->card->host->max_blk_size)
		return -EINVAL;

	if (blksz == 0) {
		blksz = min(func->max_blksize, func->card->host->max_blk_size);
		blksz = min(blksz, 512u);
	}

	ret = mmc_io_rw_direct(func->card, 1, 0,
		SDIO_FBR_BASE(func->num) + SDIO_FBR_BLKSIZE,
		blksz & 0xff, NULL);
	if (ret)
		return ret;
	ret = mmc_io_rw_direct(func->card, 1, 0,
		SDIO_FBR_BASE(func->num) + SDIO_FBR_BLKSIZE + 1,
		(blksz >> 8) & 0xff, NULL);
	if (ret)
		return ret;
	func->cur_blksize = blksz;
	return 0;
}
EXPORT_SYMBOL_GPL(sdio_set_block_size);

/*
 * Calculate the maximum byte mode transfer size
 */
static inline unsigned int sdio_max_byte_size(struct sdio_func *func)
{
	unsigned mval =	func->card->host->max_blk_size;

	if (mmc_blksz_for_byte_mode(func->card))
		mval = min(mval, func->cur_blksize);
	else
		mval = min(mval, func->max_blksize);

	if (mmc_card_broken_byte_mode_512(func->card))
		return min(mval, 511u);

	return min(mval, 512u); /* maximum size for byte mode */
}

/*
 * This is legacy code, which needs to be re-worked some day. Basically we need
 * to take into account the properties of the host, as to enable the SDIO func
 * driver layer to allocate optimal buffers.
 */
static inline unsigned int _sdio_align_size(unsigned int sz)
{
	/*
	 * FIXME: We don't have a system for the controller to tell
	 * the core about its problems yet, so for now we just 32-bit
	 * align the size.
	 */
	return ALIGN(sz, 4);
}

/**
 *	sdio_align_size - pads a transfer size to a more optimal value
 *	@func: SDIO function
 *	@sz: original transfer size
 *
 *	Pads the original data size with a number of extra bytes in
 *	order to avoid controller bugs and/or performance hits
 *	(e.g. some controllers revert to PIO for certain sizes).
 *
 *	If possible, it will also adjust the size so that it can be
 *	handled in just a single request.
 *
 *	Returns the improved size, which might be unmodified.
 */
unsigned int sdio_align_size(struct sdio_func *func, unsigned int sz)
{
	unsigned int orig_sz;
	unsigned int blk_sz, byte_sz;
	unsigned chunk_sz;

	orig_sz = sz;

	/*
	 * Do a first check with the controller, in case it
	 * wants to increase the size up to a point where it
	 * might need more than one block.
	 */
	sz = _sdio_align_size(sz);

	/*
	 * If we can still do this with just a byte transfer, then
	 * we're done.
	 */
	if (sz <= sdio_max_byte_size(func))
		return sz;

	if (func->card->cccr.multi_block) {
		/*
		 * Check if the transfer is already block aligned
		 */
		if ((sz % func->cur_blksize) == 0)
			return sz;

		/*
		 * Realign it so that it can be done with one request,
		 * and recheck if the controller still likes it.
		 */
		blk_sz = ((sz + func->cur_blksize - 1) /
			func->cur_blksize) * func->cur_blksize;
		blk_sz = _sdio_align_size(blk_sz);

		/*
		 * This value is only good if it is still just
		 * one request.
		 */
		if ((blk_sz % func->cur_blksize) == 0)
			return blk_sz;

		/*
		 * We failed to do one request, but at least try to
		 * pad the remainder properly.
		 */
		byte_sz = _sdio_align_size(sz % func->cur_blksize);
		if (byte_sz <= sdio_max_byte_size(func)) {
			blk_sz = sz / func->cur_blksize;
			return blk_sz * func->cur_blksize + byte_sz;
		}
	} else {
		/*
		 * We need multiple requests, so first check that the
		 * controller can handle the chunk size;
		 */
		chunk_sz = _sdio_align_size(sdio_max_byte_size(func));
		if (chunk_sz == sdio_max_byte_size(func)) {
			/*
			 * Fix up the size of the remainder (if any)
			 */
			byte_sz = orig_sz % chunk_sz;
			if (byte_sz) {
				byte_sz = _sdio_align_size(byte_sz);
			}

			return (orig_sz / chunk_sz) * chunk_sz + byte_sz;
		}
	}

	/*
	 * The controller is simply incapable of transferring the size
	 * we want in decent manner, so just return the original size.
	 */
	return orig_sz;
}
EXPORT_SYMBOL_GPL(sdio_align_size);

/* Split an arbitrarily sized data transfer into several
 * IO_RW_EXTENDED commands. */
static int sdio_io_rw_ext_helper(struct sdio_func *func, int write,
	unsigned addr, int incr_addr, u8 *buf, unsigned size)
{
	unsigned remainder = size;
	unsigned max_blocks;
	int ret;

	if (!func || (func->num > 7))
		return -EINVAL;

	/* Do the bulk of the transfer using block mode (if supported). */
	if (func->card->cccr.multi_block && (size > sdio_max_byte_size(func))) {
		/* Blocks per command is limited by host count, host transfer
		 * size and the maximum for IO_RW_EXTENDED of 511 blocks. */
		max_blocks = min(func->card->host->max_blk_count, 511u);

		while (remainder >= func->cur_blksize) {
			unsigned blocks;

			blocks = remainder / func->cur_blksize;
			if (blocks > max_blocks)
				blocks = max_blocks;
			size = blocks * func->cur_blksize;

			ret = mmc_io_rw_extended(func->card, write,
				func->num, addr, incr_addr, buf,
				blocks, func->cur_blksize);
			if (ret)
				return ret;

			remainder -= size;
			buf += size;
			if (incr_addr)
				addr += size;
		}
	}

	/* Write the remainder using byte mode. */
	while (remainder > 0) {
		size = min(remainder, sdio_max_byte_size(func));

		/* Indicate byte mode by setting "blocks" = 0 */
		ret = mmc_io_rw_extended(func->card, write, func->num, addr,
			 incr_addr, buf, 0, size);
		if (ret)
			return ret;

		remainder -= size;
		buf += size;
		if (incr_addr)
			addr += size;
	}
	return 0;
}

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
u8 sdio_readb(struct sdio_func *func, unsigned int addr, int *err_ret)
{
	int ret;
	u8 val;

	if (!func) {
		if (err_ret)
			*err_ret = -EINVAL;
		return 0xFF;
	}

	ret = mmc_io_rw_direct(func->card, 0, func->num, addr, 0, &val);
	if (err_ret)
		*err_ret = ret;
	if (ret)
		return 0xFF;

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
void sdio_writeb(struct sdio_func *func, u8 b, unsigned int addr, int *err_ret)
{
	int ret;

	if (!func) {
		if (err_ret)
			*err_ret = -EINVAL;
		return;
	}

	ret = mmc_io_rw_direct(func->card, 1, func->num, addr, b, NULL);
	if (err_ret)
		*err_ret = ret;
}
EXPORT_SYMBOL_GPL(sdio_writeb);

/**
 *	sdio_writeb_readb - write and read a byte from SDIO function
 *	@func: SDIO function to access
 *	@write_byte: byte to write
 *	@addr: address to write to
 *	@err_ret: optional status value from transfer
 *
 *	Performs a RAW (Read after Write) operation as defined by SDIO spec -
 *	single byte is written to address space of a given SDIO function and
 *	response is read back from the same address, both using single request.
 *	If there is a problem with the operation, 0xff is returned and
 *	@err_ret will contain the error code.
 */
u8 sdio_writeb_readb(struct sdio_func *func, u8 write_byte,
	unsigned int addr, int *err_ret)
{
	int ret;
	u8 val;

	ret = mmc_io_rw_direct(func->card, 1, func->num, addr,
			write_byte, &val);
	if (err_ret)
		*err_ret = ret;
	if (ret)
		return 0xff;

	return val;
}
EXPORT_SYMBOL_GPL(sdio_writeb_readb);

/**
 *	sdio_memcpy_fromio - read a chunk of memory from a SDIO function
 *	@func: SDIO function to access
 *	@dst: buffer to store the data
 *	@addr: address to begin reading from
 *	@count: number of bytes to read
 *
 *	Reads from the address space of a given SDIO function. Return
 *	value indicates if the transfer succeeded or not.
 */
int sdio_memcpy_fromio(struct sdio_func *func, void *dst,
	unsigned int addr, int count)
{
	return sdio_io_rw_ext_helper(func, 0, addr, 1, dst, count);
}
EXPORT_SYMBOL_GPL(sdio_memcpy_fromio);

/**
 *	sdio_memcpy_toio - write a chunk of memory to a SDIO function
 *	@func: SDIO function to access
 *	@addr: address to start writing to
 *	@src: buffer that contains the data to write
 *	@count: number of bytes to write
 *
 *	Writes to the address space of a given SDIO function. Return
 *	value indicates if the transfer succeeded or not.
 */
int sdio_memcpy_toio(struct sdio_func *func, unsigned int addr,
	void *src, int count)
{
	return sdio_io_rw_ext_helper(func, 1, addr, 1, src, count);
}
EXPORT_SYMBOL_GPL(sdio_memcpy_toio);

/**
 *	sdio_readsb - read from a FIFO on a SDIO function
 *	@func: SDIO function to access
 *	@dst: buffer to store the data
 *	@addr: address of (single byte) FIFO
 *	@count: number of bytes to read
 *
 *	Reads from the specified FIFO of a given SDIO function. Return
 *	value indicates if the transfer succeeded or not.
 */
int sdio_readsb(struct sdio_func *func, void *dst, unsigned int addr,
	int count)
{
	return sdio_io_rw_ext_helper(func, 0, addr, 0, dst, count);
}
EXPORT_SYMBOL_GPL(sdio_readsb);

/**
 *	sdio_writesb - write to a FIFO of a SDIO function
 *	@func: SDIO function to access
 *	@addr: address of (single byte) FIFO
 *	@src: buffer that contains the data to write
 *	@count: number of bytes to write
 *
 *	Writes to the specified FIFO of a given SDIO function. Return
 *	value indicates if the transfer succeeded or not.
 */
int sdio_writesb(struct sdio_func *func, unsigned int addr, void *src,
	int count)
{
	return sdio_io_rw_ext_helper(func, 1, addr, 0, src, count);
}
EXPORT_SYMBOL_GPL(sdio_writesb);

/**
 *	sdio_readw - read a 16 bit integer from a SDIO function
 *	@func: SDIO function to access
 *	@addr: address to read
 *	@err_ret: optional status value from transfer
 *
 *	Reads a 16 bit integer from the address space of a given SDIO
 *	function. If there is a problem reading the address, 0xffff
 *	is returned and @err_ret will contain the error code.
 */
u16 sdio_readw(struct sdio_func *func, unsigned int addr, int *err_ret)
{
	int ret;

	ret = sdio_memcpy_fromio(func, func->tmpbuf, addr, 2);
	if (err_ret)
		*err_ret = ret;
	if (ret)
		return 0xFFFF;

	return le16_to_cpup((__le16 *)func->tmpbuf);
}
EXPORT_SYMBOL_GPL(sdio_readw);

/**
 *	sdio_writew - write a 16 bit integer to a SDIO function
 *	@func: SDIO function to access
 *	@b: integer to write
 *	@addr: address to write to
 *	@err_ret: optional status value from transfer
 *
 *	Writes a 16 bit integer to the address space of a given SDIO
 *	function. @err_ret will contain the status of the actual
 *	transfer.
 */
void sdio_writew(struct sdio_func *func, u16 b, unsigned int addr, int *err_ret)
{
	int ret;

	*(__le16 *)func->tmpbuf = cpu_to_le16(b);

	ret = sdio_memcpy_toio(func, addr, func->tmpbuf, 2);
	if (err_ret)
		*err_ret = ret;
}
EXPORT_SYMBOL_GPL(sdio_writew);

/**
 *	sdio_readl - read a 32 bit integer from a SDIO function
 *	@func: SDIO function to access
 *	@addr: address to read
 *	@err_ret: optional status value from transfer
 *
 *	Reads a 32 bit integer from the address space of a given SDIO
 *	function. If there is a problem reading the address,
 *	0xffffffff is returned and @err_ret will contain the error
 *	code.
 */
u32 sdio_readl(struct sdio_func *func, unsigned int addr, int *err_ret)
{
	int ret;

	ret = sdio_memcpy_fromio(func, func->tmpbuf, addr, 4);
	if (err_ret)
		*err_ret = ret;
	if (ret)
		return 0xFFFFFFFF;

	return le32_to_cpup((__le32 *)func->tmpbuf);
}
EXPORT_SYMBOL_GPL(sdio_readl);

/**
 *	sdio_writel - write a 32 bit integer to a SDIO function
 *	@func: SDIO function to access
 *	@b: integer to write
 *	@addr: address to write to
 *	@err_ret: optional status value from transfer
 *
 *	Writes a 32 bit integer to the address space of a given SDIO
 *	function. @err_ret will contain the status of the actual
 *	transfer.
 */
void sdio_writel(struct sdio_func *func, u32 b, unsigned int addr, int *err_ret)
{
	int ret;

	*(__le32 *)func->tmpbuf = cpu_to_le32(b);

	ret = sdio_memcpy_toio(func, addr, func->tmpbuf, 4);
	if (err_ret)
		*err_ret = ret;
}
EXPORT_SYMBOL_GPL(sdio_writel);

/**
 *	sdio_f0_readb - read a single byte from SDIO function 0
 *	@func: an SDIO function of the card
 *	@addr: address to read
 *	@err_ret: optional status value from transfer
 *
 *	Reads a single byte from the address space of SDIO function 0.
 *	If there is a problem reading the address, 0xff is returned
 *	and @err_ret will contain the error code.
 */
unsigned char sdio_f0_readb(struct sdio_func *func, unsigned int addr,
	int *err_ret)
{
	int ret;
	unsigned char val;

	if (!func) {
		if (err_ret)
			*err_ret = -EINVAL;
		return 0xFF;
	}

	ret = mmc_io_rw_direct(func->card, 0, 0, addr, 0, &val);
	if (err_ret)
		*err_ret = ret;
	if (ret)
		return 0xFF;

	return val;
}
EXPORT_SYMBOL_GPL(sdio_f0_readb);

/**
 *	sdio_f0_writeb - write a single byte to SDIO function 0
 *	@func: an SDIO function of the card
 *	@b: byte to write
 *	@addr: address to write to
 *	@err_ret: optional status value from transfer
 *
 *	Writes a single byte to the address space of SDIO function 0.
 *	@err_ret will contain the status of the actual transfer.
 *
 *	Only writes to the vendor specific CCCR registers (0xF0 -
 *	0xFF) are permiited; @err_ret will be set to -EINVAL for *
 *	writes outside this range.
 */
void sdio_f0_writeb(struct sdio_func *func, unsigned char b, unsigned int addr,
	int *err_ret)
{
	int ret;

	if (!func) {
		if (err_ret)
			*err_ret = -EINVAL;
		return;
	}

	if ((addr < 0xF0 || addr > 0xFF) && (!mmc_card_lenient_fn0(func->card))) {
		if (err_ret)
			*err_ret = -EINVAL;
		return;
	}

	ret = mmc_io_rw_direct(func->card, 1, 0, addr, b, NULL);
	if (err_ret)
		*err_ret = ret;
}
EXPORT_SYMBOL_GPL(sdio_f0_writeb);

/**
 *	sdio_get_host_pm_caps - get host power management capabilities
 *	@func: SDIO function attached to host
 *
 *	Returns a capability bitmask corresponding to power management
 *	features supported by the host controller that the card function
 *	might rely upon during a system suspend.  The host doesn't need
 *	to be claimed, nor the function active, for this information to be
 *	obtained.
 */
mmc_pm_flag_t sdio_get_host_pm_caps(struct sdio_func *func)
{
	if (!func)
		return 0;

	return func->card->host->pm_caps;
}
EXPORT_SYMBOL_GPL(sdio_get_host_pm_caps);

/**
 *	sdio_set_host_pm_flags - set wanted host power management capabilities
 *	@func: SDIO function attached to host
 *
 *	Set a capability bitmask corresponding to wanted host controller
 *	power management features for the upcoming suspend state.
 *	This must be called, if needed, each time the suspend method of
 *	the function driver is called, and must contain only bits that
 *	were returned by sdio_get_host_pm_caps().
 *	The host doesn't need to be claimed, nor the function active,
 *	for this information to be set.
 */
int sdio_set_host_pm_flags(struct sdio_func *func, mmc_pm_flag_t flags)
{
	struct mmc_host *host;

	if (!func)
		return -EINVAL;

	host = func->card->host;

	if (flags & ~host->pm_caps)
		return -EINVAL;

	/* function suspend methods are serialized, hence no lock needed */
	host->pm_flags |= flags;
	return 0;
}
EXPORT_SYMBOL_GPL(sdio_set_host_pm_flags);
