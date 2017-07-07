/*
 * Copyright (C) 2015-2017 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * nfp_cpplib.c
 * Library of functions to access the NFP's CPP bus
 * Authors: Jakub Kicinski <jakub.kicinski@netronome.com>
 *          Jason McMullan <jason.mcmullan@netronome.com>
 *          Rolf Neugebauer <rolf.neugebauer@netronome.com>
 */

#include <asm/unaligned.h>
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sched.h>

#include "nfp_cpp.h"
#include "nfp6000/nfp6000.h"
#include "nfp6000/nfp_xpb.h"

/* NFP6000 PL */
#define NFP_PL_DEVICE_ID			0x00000004
#define   NFP_PL_DEVICE_ID_MASK			GENMASK(7, 0)

#define NFP6000_ARM_GCSR_SOFTMODEL0		0x00400144

/**
 * nfp_cpp_readl() - Read a u32 word from a CPP location
 * @cpp:	CPP device handle
 * @cpp_id:	CPP ID for operation
 * @address:	Address for operation
 * @value:	Pointer to read buffer
 *
 * Return: length of the io, or -ERRNO
 */
int nfp_cpp_readl(struct nfp_cpp *cpp, u32 cpp_id,
		  unsigned long long address, u32 *value)
{
	u8 tmp[4];
	int err;

	err = nfp_cpp_read(cpp, cpp_id, address, tmp, sizeof(tmp));
	*value = get_unaligned_le32(tmp);

	return err;
}

/**
 * nfp_cpp_writel() - Write a u32 word to a CPP location
 * @cpp:	CPP device handle
 * @cpp_id:	CPP ID for operation
 * @address:	Address for operation
 * @value:	Value to write
 *
 * Return: length of the io, or -ERRNO
 */
int nfp_cpp_writel(struct nfp_cpp *cpp, u32 cpp_id,
		   unsigned long long address, u32 value)
{
	u8 tmp[4];

	put_unaligned_le32(value, tmp);
	return nfp_cpp_write(cpp, cpp_id, address, tmp, sizeof(tmp));
}

/**
 * nfp_cpp_readq() - Read a u64 word from a CPP location
 * @cpp:	CPP device handle
 * @cpp_id:	CPP ID for operation
 * @address:	Address for operation
 * @value:	Pointer to read buffer
 *
 * Return: length of the io, or -ERRNO
 */
int nfp_cpp_readq(struct nfp_cpp *cpp, u32 cpp_id,
		  unsigned long long address, u64 *value)
{
	u8 tmp[8];
	int err;

	err = nfp_cpp_read(cpp, cpp_id, address, tmp, sizeof(tmp));
	*value = get_unaligned_le64(tmp);

	return err;
}

/**
 * nfp_cpp_writeq() - Write a u64 word to a CPP location
 * @cpp:	CPP device handle
 * @cpp_id:	CPP ID for operation
 * @address:	Address for operation
 * @value:	Value to write
 *
 * Return: length of the io, or -ERRNO
 */
int nfp_cpp_writeq(struct nfp_cpp *cpp, u32 cpp_id,
		   unsigned long long address, u64 value)
{
	u8 tmp[8];

	put_unaligned_le64(value, tmp);
	return nfp_cpp_write(cpp, cpp_id, address, tmp, sizeof(tmp));
}

/* NOTE: This code should not use nfp_xpb_* functions,
 * as those are model-specific
 */
int nfp_cpp_model_autodetect(struct nfp_cpp *cpp, u32 *model)
{
	const u32 arm_id = NFP_CPP_ID(NFP_CPP_TARGET_ARM, 0, 0);
	u32 reg;
	int err;

	err = nfp_cpp_readl(cpp, arm_id, NFP6000_ARM_GCSR_SOFTMODEL0, model);
	if (err < 0)
		return err;

	/* The PL's PluDeviceID revision code is authoratative */
	*model &= ~0xff;
	err = nfp_xpb_readl(cpp, NFP_XPB_DEVICE(1, 1, 16) + NFP_PL_DEVICE_ID,
			    &reg);
	if (err < 0)
		return err;

	*model |= (NFP_PL_DEVICE_ID_MASK & reg) - 0x10;

	return 0;
}

static u8 nfp_bytemask(int width, u64 addr)
{
	if (width == 8)
		return 0xff;
	else if (width == 4)
		return 0x0f << (addr & 4);
	else if (width == 2)
		return 0x03 << (addr & 6);
	else if (width == 1)
		return 0x01 << (addr & 7);
	else
		return 0;
}

int nfp_cpp_explicit_read(struct nfp_cpp *cpp, u32 cpp_id,
			  u64 addr, void *buff, size_t len, int width_read)
{
	struct nfp_cpp_explicit *expl;
	char *tmp = buff;
	int err, i, incr;
	u8 byte_mask;

	if (len & (width_read - 1))
		return -EINVAL;

	expl = nfp_cpp_explicit_acquire(cpp);
	if (!expl)
		return -EBUSY;

	incr = min_t(int, 16 * width_read, 128);
	incr = min_t(int, incr, len);

	/* Translate a NFP_CPP_ACTION_RW to action 0 */
	if (NFP_CPP_ID_ACTION_of(cpp_id) == NFP_CPP_ACTION_RW)
		cpp_id = NFP_CPP_ID(NFP_CPP_ID_TARGET_of(cpp_id), 0,
				    NFP_CPP_ID_TOKEN_of(cpp_id));

	byte_mask = nfp_bytemask(width_read, addr);

	nfp_cpp_explicit_set_target(expl, cpp_id,
				    incr / width_read - 1, byte_mask);
	nfp_cpp_explicit_set_posted(expl, 1, 0, NFP_SIGNAL_PUSH,
				    0, NFP_SIGNAL_NONE);

	for (i = 0; i < len; i += incr, addr += incr, tmp += incr) {
		if (i + incr > len) {
			incr = len - i;
			nfp_cpp_explicit_set_target(expl, cpp_id,
						    incr / width_read - 1,
						    0xff);
		}

		err = nfp_cpp_explicit_do(expl, addr);
		if (err < 0)
			goto exit_release;

		err = nfp_cpp_explicit_get(expl, tmp, incr);
		if (err < 0)
			goto exit_release;
	}
	err = len;
exit_release:
	nfp_cpp_explicit_release(expl);

	return err;
}

int nfp_cpp_explicit_write(struct nfp_cpp *cpp, u32 cpp_id, u64 addr,
			   const void *buff, size_t len, int width_write)
{
	struct nfp_cpp_explicit *expl;
	const char *tmp = buff;
	int err, i, incr;
	u8 byte_mask;

	if (len & (width_write - 1))
		return -EINVAL;

	expl = nfp_cpp_explicit_acquire(cpp);
	if (!expl)
		return -EBUSY;

	incr = min_t(int, 16 * width_write, 128);
	incr = min_t(int, incr, len);

	/* Translate a NFP_CPP_ACTION_RW to action 1 */
	if (NFP_CPP_ID_ACTION_of(cpp_id) == NFP_CPP_ACTION_RW)
		cpp_id = NFP_CPP_ID(NFP_CPP_ID_TARGET_of(cpp_id), 1,
				    NFP_CPP_ID_TOKEN_of(cpp_id));

	byte_mask = nfp_bytemask(width_write, addr);

	nfp_cpp_explicit_set_target(expl, cpp_id,
				    incr / width_write - 1, byte_mask);
	nfp_cpp_explicit_set_posted(expl, 1, 0, NFP_SIGNAL_PULL,
				    0, NFP_SIGNAL_NONE);

	for (i = 0; i < len; i += incr, addr += incr, tmp += incr) {
		if (i + incr > len) {
			incr = len - i;
			nfp_cpp_explicit_set_target(expl, cpp_id,
						    incr / width_write - 1,
						    0xff);
		}

		err = nfp_cpp_explicit_put(expl, tmp, incr);
		if (err < 0)
			goto exit_release;

		err = nfp_cpp_explicit_do(expl, addr);
		if (err < 0)
			goto exit_release;
	}
	err = len;
exit_release:
	nfp_cpp_explicit_release(expl);

	return err;
}

/**
 * nfp_cpp_map_area() - Helper function to map an area
 * @cpp:    NFP CPP handler
 * @name:   Name for the area
 * @domain: CPP domain
 * @target: CPP target
 * @addr:   CPP address
 * @size:   Size of the area
 * @area:   Area handle (output)
 *
 * Map an area of IOMEM access.  To undo the effect of this function call
 * @nfp_cpp_area_release_free(*area).
 *
 * Return: Pointer to memory mapped area or ERR_PTR
 */
u8 __iomem *
nfp_cpp_map_area(struct nfp_cpp *cpp, const char *name, int domain, int target,
		 u64 addr, unsigned long size, struct nfp_cpp_area **area)
{
	u8 __iomem *res;
	u32 dest;

	dest = NFP_CPP_ISLAND_ID(target, NFP_CPP_ACTION_RW, 0, domain);

	*area = nfp_cpp_area_alloc_acquire(cpp, name, dest, addr, size);
	if (!*area)
		goto err_eio;

	res = nfp_cpp_area_iomem(*area);
	if (!res)
		goto err_release_free;

	return res;

err_release_free:
	nfp_cpp_area_release_free(*area);
err_eio:
	return (u8 __iomem *)ERR_PTR(-EIO);
}
