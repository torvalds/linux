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
 * nfp_nsp.c
 * Author: Jakub Kicinski <jakub.kicinski@netronome.com>
 *         Jason McMullan <jason.mcmullan@netronome.com>
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/sizes.h>
#include <linux/slab.h>

#define NFP_SUBSYS "nfp_nsp"

#include "nfp.h"
#include "nfp_cpp.h"

/* Offsets relative to the CSR base */
#define NSP_STATUS		0x00
#define   NSP_STATUS_MAGIC	GENMASK_ULL(63, 48)
#define   NSP_STATUS_MAJOR	GENMASK_ULL(47, 44)
#define   NSP_STATUS_MINOR	GENMASK_ULL(43, 32)
#define   NSP_STATUS_CODE	GENMASK_ULL(31, 16)
#define   NSP_STATUS_RESULT	GENMASK_ULL(15, 8)
#define   NSP_STATUS_BUSY	BIT_ULL(0)

#define NSP_COMMAND		0x08
#define   NSP_COMMAND_OPTION	GENMASK_ULL(63, 32)
#define   NSP_COMMAND_CODE	GENMASK_ULL(31, 16)
#define   NSP_COMMAND_START	BIT_ULL(0)

/* CPP address to retrieve the data from */
#define NSP_BUFFER		0x10
#define   NSP_BUFFER_CPP	GENMASK_ULL(63, 40)
#define   NSP_BUFFER_PCIE	GENMASK_ULL(39, 38)
#define   NSP_BUFFER_ADDRESS	GENMASK_ULL(37, 0)

#define NSP_DFLT_BUFFER		0x18

#define NSP_DFLT_BUFFER_CONFIG	0x20
#define   NSP_DFLT_BUFFER_SIZE_MB	GENMASK_ULL(7, 0)

#define NSP_MAGIC		0xab10
#define NSP_MAJOR		0
#define NSP_MINOR		(__MAX_SPCODE - 1)

#define NSP_CODE_MAJOR		GENMASK(15, 12)
#define NSP_CODE_MINOR		GENMASK(11, 0)

enum nfp_nsp_cmd {
	SPCODE_NOOP		= 0, /* No operation */
	SPCODE_SOFT_RESET	= 1, /* Soft reset the NFP */
	SPCODE_FW_DEFAULT	= 2, /* Load default (UNDI) FW */
	SPCODE_PHY_INIT		= 3, /* Initialize the PHY */
	SPCODE_MAC_INIT		= 4, /* Initialize the MAC */
	SPCODE_PHY_RXADAPT	= 5, /* Re-run PHY RX Adaptation */
	SPCODE_FW_LOAD		= 6, /* Load fw from buffer, len in option */
	SPCODE_ETH_RESCAN	= 7, /* Rescan ETHs, write ETH_TABLE to buf */
	SPCODE_ETH_CONTROL	= 8, /* Update media config from buffer */

	__MAX_SPCODE,
};

struct nfp_nsp {
	struct nfp_cpp *cpp;
	struct nfp_resource *res;
	struct {
		u16 major;
		u16 minor;
	} ver;
};

static int nfp_nsp_check(struct nfp_nsp *state)
{
	struct nfp_cpp *cpp = state->cpp;
	u64 nsp_status, reg;
	u32 nsp_cpp;
	int err;

	nsp_cpp = nfp_resource_cpp_id(state->res);
	nsp_status = nfp_resource_address(state->res) + NSP_STATUS;

	err = nfp_cpp_readq(cpp, nsp_cpp, nsp_status, &reg);
	if (err < 0)
		return err;

	if (FIELD_GET(NSP_STATUS_MAGIC, reg) != NSP_MAGIC) {
		nfp_err(cpp, "Cannot detect NFP Service Processor\n");
		return -ENODEV;
	}

	state->ver.major = FIELD_GET(NSP_STATUS_MAJOR, reg);
	state->ver.minor = FIELD_GET(NSP_STATUS_MINOR, reg);

	if (state->ver.major != NSP_MAJOR || state->ver.minor < NSP_MINOR) {
		nfp_err(cpp, "Unsupported ABI %hu.%hu\n",
			state->ver.major, state->ver.minor);
		return -EINVAL;
	}

	if (reg & NSP_STATUS_BUSY) {
		nfp_err(cpp, "Service processor busy!\n");
		return -EBUSY;
	}

	return 0;
}

/**
 * nfp_nsp_open() - Prepare for communication and lock the NSP resource.
 * @cpp:	NFP CPP Handle
 */
struct nfp_nsp *nfp_nsp_open(struct nfp_cpp *cpp)
{
	struct nfp_resource *res;
	struct nfp_nsp *state;
	int err;

	res = nfp_resource_acquire(cpp, NFP_RESOURCE_NSP);
	if (IS_ERR(res))
		return (void *)res;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state) {
		nfp_resource_release(res);
		return ERR_PTR(-ENOMEM);
	}
	state->cpp = cpp;
	state->res = res;

	err = nfp_nsp_check(state);
	if (err) {
		nfp_nsp_close(state);
		return ERR_PTR(err);
	}

	return state;
}

/**
 * nfp_nsp_close() - Clean up and unlock the NSP resource.
 * @state:	NFP SP state
 */
void nfp_nsp_close(struct nfp_nsp *state)
{
	nfp_resource_release(state->res);
	kfree(state);
}

u16 nfp_nsp_get_abi_ver_major(struct nfp_nsp *state)
{
	return state->ver.major;
}

u16 nfp_nsp_get_abi_ver_minor(struct nfp_nsp *state)
{
	return state->ver.minor;
}

static int
nfp_nsp_wait_reg(struct nfp_cpp *cpp, u64 *reg,
		 u32 nsp_cpp, u64 addr, u64 mask, u64 val)
{
	const unsigned long wait_until = jiffies + 30 * HZ;
	int err;

	for (;;) {
		const unsigned long start_time = jiffies;

		err = nfp_cpp_readq(cpp, nsp_cpp, addr, reg);
		if (err < 0)
			return err;

		if ((*reg & mask) == val)
			return 0;

		err = msleep_interruptible(100);
		if (err)
			return err;

		if (time_after(start_time, wait_until))
			return -ETIMEDOUT;
	}
}

/**
 * nfp_nsp_command() - Execute a command on the NFP Service Processor
 * @state:	NFP SP state
 * @code:	NFP SP Command Code
 * @option:	NFP SP Command Argument
 * @buff_cpp:	NFP SP Buffer CPP Address info
 * @buff_addr:	NFP SP Buffer Host address
 *
 * Return: 0 for success with no result
 *
 *	 1..255 for NSP completion with a result code
 *
 *	-EAGAIN if the NSP is not yet present
 *	-ENODEV if the NSP is not a supported model
 *	-EBUSY if the NSP is stuck
 *	-EINTR if interrupted while waiting for completion
 *	-ETIMEDOUT if the NSP took longer than 30 seconds to complete
 */
static int nfp_nsp_command(struct nfp_nsp *state, u16 code, u32 option,
			   u32 buff_cpp, u64 buff_addr)
{
	u64 reg, nsp_base, nsp_buffer, nsp_status, nsp_command;
	struct nfp_cpp *cpp = state->cpp;
	u32 nsp_cpp;
	int err;

	nsp_cpp = nfp_resource_cpp_id(state->res);
	nsp_base = nfp_resource_address(state->res);
	nsp_status = nsp_base + NSP_STATUS;
	nsp_command = nsp_base + NSP_COMMAND;
	nsp_buffer = nsp_base + NSP_BUFFER;

	err = nfp_nsp_check(state);
	if (err)
		return err;

	if (!FIELD_FIT(NSP_BUFFER_CPP, buff_cpp >> 8) ||
	    !FIELD_FIT(NSP_BUFFER_ADDRESS, buff_addr)) {
		nfp_err(cpp, "Host buffer out of reach %08x %016llx\n",
			buff_cpp, buff_addr);
		return -EINVAL;
	}

	err = nfp_cpp_writeq(cpp, nsp_cpp, nsp_buffer,
			     FIELD_PREP(NSP_BUFFER_CPP, buff_cpp >> 8) |
			     FIELD_PREP(NSP_BUFFER_ADDRESS, buff_addr));
	if (err < 0)
		return err;

	err = nfp_cpp_writeq(cpp, nsp_cpp, nsp_command,
			     FIELD_PREP(NSP_COMMAND_OPTION, option) |
			     FIELD_PREP(NSP_COMMAND_CODE, code) |
			     FIELD_PREP(NSP_COMMAND_START, 1));
	if (err < 0)
		return err;

	/* Wait for NSP_COMMAND_START to go to 0 */
	err = nfp_nsp_wait_reg(cpp, &reg,
			       nsp_cpp, nsp_command, NSP_COMMAND_START, 0);
	if (err) {
		nfp_err(cpp, "Error %d waiting for code 0x%04x to start\n",
			err, code);
		return err;
	}

	/* Wait for NSP_STATUS_BUSY to go to 0 */
	err = nfp_nsp_wait_reg(cpp, &reg,
			       nsp_cpp, nsp_status, NSP_STATUS_BUSY, 0);
	if (err) {
		nfp_err(cpp, "Error %d waiting for code 0x%04x to complete\n",
			err, code);
		return err;
	}

	err = FIELD_GET(NSP_STATUS_RESULT, reg);
	if (err) {
		nfp_warn(cpp, "Result (error) code set: %d command: %d\n",
			 -err, code);
		return -err;
	}

	err = nfp_cpp_readq(cpp, nsp_cpp, nsp_command, &reg);
	if (err < 0)
		return err;

	return FIELD_GET(NSP_COMMAND_OPTION, reg);
}

static int nfp_nsp_command_buf(struct nfp_nsp *nsp, u16 code, u32 option,
			       const void *in_buf, unsigned int in_size,
			       void *out_buf, unsigned int out_size)
{
	struct nfp_cpp *cpp = nsp->cpp;
	unsigned int max_size;
	u64 reg, cpp_buf;
	int ret, err;
	u32 cpp_id;

	if (nsp->ver.minor < 13) {
		nfp_err(cpp, "NSP: Code 0x%04x with buffer not supported (ABI %hu.%hu)\n",
			code, nsp->ver.major, nsp->ver.minor);
		return -EOPNOTSUPP;
	}

	err = nfp_cpp_readq(cpp, nfp_resource_cpp_id(nsp->res),
			    nfp_resource_address(nsp->res) +
			    NSP_DFLT_BUFFER_CONFIG,
			    &reg);
	if (err < 0)
		return err;

	max_size = max(in_size, out_size);
	if (FIELD_GET(NSP_DFLT_BUFFER_SIZE_MB, reg) * SZ_1M < max_size) {
		nfp_err(cpp, "NSP: default buffer too small for command 0x%04x (%llu < %u)\n",
			code, FIELD_GET(NSP_DFLT_BUFFER_SIZE_MB, reg) * SZ_1M,
			max_size);
		return -EINVAL;
	}

	err = nfp_cpp_readq(cpp, nfp_resource_cpp_id(nsp->res),
			    nfp_resource_address(nsp->res) +
			    NSP_DFLT_BUFFER,
			    &reg);
	if (err < 0)
		return err;

	cpp_id = FIELD_GET(NSP_BUFFER_CPP, reg) << 8;
	cpp_buf = FIELD_GET(NSP_BUFFER_ADDRESS, reg);

	if (in_buf && in_size) {
		err = nfp_cpp_write(cpp, cpp_id, cpp_buf, in_buf, in_size);
		if (err < 0)
			return err;
	}

	ret = nfp_nsp_command(nsp, code, option, cpp_id, cpp_buf);
	if (ret < 0)
		return ret;

	if (out_buf && out_size) {
		err = nfp_cpp_read(cpp, cpp_id, cpp_buf, out_buf, out_size);
		if (err < 0)
			return err;
	}

	return ret;
}

int nfp_nsp_wait(struct nfp_nsp *state)
{
	const unsigned long wait_until = jiffies + 30 * HZ;
	int err;

	nfp_dbg(state->cpp, "Waiting for NSP to respond (30 sec max).\n");

	for (;;) {
		const unsigned long start_time = jiffies;

		err = nfp_nsp_command(state, SPCODE_NOOP, 0, 0, 0);
		if (err != -EAGAIN)
			break;

		err = msleep_interruptible(100);
		if (err)
			break;

		if (time_after(start_time, wait_until)) {
			err = -ETIMEDOUT;
			break;
		}
	}
	if (err)
		nfp_err(state->cpp, "NSP failed to respond %d\n", err);

	return err;
}

int nfp_nsp_device_soft_reset(struct nfp_nsp *state)
{
	int err;

	err = nfp_nsp_command(state, SPCODE_SOFT_RESET, 0, 0, 0);

	nfp_nffw_cache_flush(state->cpp);

	return err;
}

int nfp_nsp_load_fw(struct nfp_nsp *state, const struct firmware *fw)
{
	return nfp_nsp_command_buf(state, SPCODE_FW_LOAD, fw->size, fw->data,
				   fw->size, NULL, 0);
}

int nfp_nsp_read_eth_table(struct nfp_nsp *state, void *buf, unsigned int size)
{
	return nfp_nsp_command_buf(state, SPCODE_ETH_RESCAN, size, NULL, 0,
				   buf, size);
}

int nfp_nsp_write_eth_table(struct nfp_nsp *state,
			    const void *buf, unsigned int size)
{
	return nfp_nsp_command_buf(state, SPCODE_ETH_CONTROL, size, buf, size,
				   NULL, 0);
}
