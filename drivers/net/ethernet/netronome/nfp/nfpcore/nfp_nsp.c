// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2015-2018 Netronome Systems, Inc. */

/*
 * nfp_nsp.c
 * Author: Jakub Kicinski <jakub.kicinski@netronome.com>
 *         Jason McMullan <jason.mcmullan@netronome.com>
 */

#include <asm/unaligned.h>
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/overflow.h>
#include <linux/sizes.h>
#include <linux/slab.h>

#define NFP_SUBSYS "nfp_nsp"

#include "nfp.h"
#include "nfp_cpp.h"
#include "nfp_nsp.h"

#define NFP_NSP_TIMEOUT_DEFAULT	30
#define NFP_NSP_TIMEOUT_BOOT	30

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
#define   NSP_COMMAND_DMA_BUF	BIT_ULL(1)
#define   NSP_COMMAND_START	BIT_ULL(0)

/* CPP address to retrieve the data from */
#define NSP_BUFFER		0x10
#define   NSP_BUFFER_CPP	GENMASK_ULL(63, 40)
#define   NSP_BUFFER_ADDRESS	GENMASK_ULL(39, 0)

#define NSP_DFLT_BUFFER		0x18
#define   NSP_DFLT_BUFFER_CPP	GENMASK_ULL(63, 40)
#define   NSP_DFLT_BUFFER_ADDRESS	GENMASK_ULL(39, 0)

#define NSP_DFLT_BUFFER_CONFIG	0x20
#define   NSP_DFLT_BUFFER_DMA_CHUNK_ORDER	GENMASK_ULL(63, 58)
#define   NSP_DFLT_BUFFER_SIZE_4KB	GENMASK_ULL(15, 8)
#define   NSP_DFLT_BUFFER_SIZE_MB	GENMASK_ULL(7, 0)

#define NFP_CAP_CMD_DMA_SG	0x28

#define NSP_MAGIC		0xab10
#define NSP_MAJOR		0
#define NSP_MINOR		8

#define NSP_CODE_MAJOR		GENMASK(15, 12)
#define NSP_CODE_MINOR		GENMASK(11, 0)

#define NFP_FW_LOAD_RET_MAJOR	GENMASK(15, 8)
#define NFP_FW_LOAD_RET_MINOR	GENMASK(23, 16)

#define NFP_HWINFO_LOOKUP_SIZE	GENMASK(11, 0)

#define NFP_VERSIONS_SIZE	GENMASK(11, 0)
#define NFP_VERSIONS_CNT_OFF	0
#define NFP_VERSIONS_BSP_OFF	2
#define NFP_VERSIONS_CPLD_OFF	6
#define NFP_VERSIONS_APP_OFF	10
#define NFP_VERSIONS_BUNDLE_OFF	14
#define NFP_VERSIONS_UNDI_OFF	18
#define NFP_VERSIONS_NCSI_OFF	22
#define NFP_VERSIONS_CFGR_OFF	26

#define NSP_SFF_EEPROM_BLOCK_LEN	8

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
	SPCODE_NSP_WRITE_FLASH	= 11, /* Load and flash image from buffer */
	SPCODE_NSP_SENSORS	= 12, /* Read NSP sensor(s) */
	SPCODE_NSP_IDENTIFY	= 13, /* Read NSP version */
	SPCODE_FW_STORED	= 16, /* If no FW loaded, load flash app FW */
	SPCODE_HWINFO_LOOKUP	= 17, /* Lookup HWinfo with overwrites etc. */
	SPCODE_VERSIONS		= 21, /* Report FW versions */
	SPCODE_READ_SFF_EEPROM	= 22, /* Read module EEPROM */
};

struct nfp_nsp_dma_buf {
	__le32 chunk_cnt;
	__le32 reserved[3];
	struct {
		__le32 size;
		__le32 reserved;
		__le64 addr;
	} descs[];
};

static const struct {
	int code;
	const char *msg;
} nsp_errors[] = {
	{ 6010, "could not map to phy for port" },
	{ 6011, "not an allowed rate/lanes for port" },
	{ 6012, "not an allowed rate/lanes for port" },
	{ 6013, "high/low error, change other port first" },
	{ 6014, "config not found in flash" },
};

struct nfp_nsp {
	struct nfp_cpp *cpp;
	struct nfp_resource *res;
	struct {
		u16 major;
		u16 minor;
	} ver;

	/* Eth table config state */
	bool modified;
	unsigned int idx;
	void *entries;
};

/**
 * struct nfp_nsp_command_arg - NFP command argument structure
 * @code:	NFP SP Command Code
 * @dma:	@buf points to a host buffer, not NSP buffer
 * @timeout_sec:Timeout value to wait for completion in seconds
 * @option:	NFP SP Command Argument
 * @buf:	NFP SP Buffer Address
 * @error_cb:	Callback for interpreting option if error occurred
 */
struct nfp_nsp_command_arg {
	u16 code;
	bool dma;
	unsigned int timeout_sec;
	u32 option;
	u64 buf;
	void (*error_cb)(struct nfp_nsp *state, u32 ret_val);
};

/**
 * struct nfp_nsp_command_buf_arg - NFP command with buffer argument structure
 * @arg:	NFP command argument structure
 * @in_buf:	Buffer with data for input
 * @in_size:	Size of @in_buf
 * @out_buf:	Buffer for output data
 * @out_size:	Size of @out_buf
 */
struct nfp_nsp_command_buf_arg {
	struct nfp_nsp_command_arg arg;
	const void *in_buf;
	unsigned int in_size;
	void *out_buf;
	unsigned int out_size;
};

struct nfp_cpp *nfp_nsp_cpp(struct nfp_nsp *state)
{
	return state->cpp;
}

bool nfp_nsp_config_modified(struct nfp_nsp *state)
{
	return state->modified;
}

void nfp_nsp_config_set_modified(struct nfp_nsp *state, bool modified)
{
	state->modified = modified;
}

void *nfp_nsp_config_entries(struct nfp_nsp *state)
{
	return state->entries;
}

unsigned int nfp_nsp_config_idx(struct nfp_nsp *state)
{
	return state->idx;
}

void
nfp_nsp_config_set_state(struct nfp_nsp *state, void *entries, unsigned int idx)
{
	state->entries = entries;
	state->idx = idx;
}

void nfp_nsp_config_clear_state(struct nfp_nsp *state)
{
	state->entries = NULL;
	state->idx = 0;
}

static void nfp_nsp_print_extended_error(struct nfp_nsp *state, u32 ret_val)
{
	int i;

	if (!ret_val)
		return;

	for (i = 0; i < ARRAY_SIZE(nsp_errors); i++)
		if (ret_val == nsp_errors[i].code)
			nfp_err(state->cpp, "err msg: %s\n", nsp_errors[i].msg);
}

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
nfp_nsp_wait_reg(struct nfp_cpp *cpp, u64 *reg, u32 nsp_cpp, u64 addr,
		 u64 mask, u64 val, u32 timeout_sec)
{
	const unsigned long wait_until = jiffies + timeout_sec * HZ;
	int err;

	for (;;) {
		const unsigned long start_time = jiffies;

		err = nfp_cpp_readq(cpp, nsp_cpp, addr, reg);
		if (err < 0)
			return err;

		if ((*reg & mask) == val)
			return 0;

		msleep(25);

		if (time_after(start_time, wait_until))
			return -ETIMEDOUT;
	}
}

/**
 * __nfp_nsp_command() - Execute a command on the NFP Service Processor
 * @state:	NFP SP state
 * @arg:	NFP command argument structure
 *
 * Return: 0 for success with no result
 *
 *	 positive value for NSP completion with a result code
 *
 *	-EAGAIN if the NSP is not yet present
 *	-ENODEV if the NSP is not a supported model
 *	-EBUSY if the NSP is stuck
 *	-EINTR if interrupted while waiting for completion
 *	-ETIMEDOUT if the NSP took longer than @timeout_sec seconds to complete
 */
static int
__nfp_nsp_command(struct nfp_nsp *state, const struct nfp_nsp_command_arg *arg)
{
	u64 reg, ret_val, nsp_base, nsp_buffer, nsp_status, nsp_command;
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

	err = nfp_cpp_writeq(cpp, nsp_cpp, nsp_buffer, arg->buf);
	if (err < 0)
		return err;

	err = nfp_cpp_writeq(cpp, nsp_cpp, nsp_command,
			     FIELD_PREP(NSP_COMMAND_OPTION, arg->option) |
			     FIELD_PREP(NSP_COMMAND_CODE, arg->code) |
			     FIELD_PREP(NSP_COMMAND_DMA_BUF, arg->dma) |
			     FIELD_PREP(NSP_COMMAND_START, 1));
	if (err < 0)
		return err;

	/* Wait for NSP_COMMAND_START to go to 0 */
	err = nfp_nsp_wait_reg(cpp, &reg, nsp_cpp, nsp_command,
			       NSP_COMMAND_START, 0, NFP_NSP_TIMEOUT_DEFAULT);
	if (err) {
		nfp_err(cpp, "Error %d waiting for code 0x%04x to start\n",
			err, arg->code);
		return err;
	}

	/* Wait for NSP_STATUS_BUSY to go to 0 */
	err = nfp_nsp_wait_reg(cpp, &reg, nsp_cpp, nsp_status, NSP_STATUS_BUSY,
			       0, arg->timeout_sec ?: NFP_NSP_TIMEOUT_DEFAULT);
	if (err) {
		nfp_err(cpp, "Error %d waiting for code 0x%04x to complete\n",
			err, arg->code);
		return err;
	}

	err = nfp_cpp_readq(cpp, nsp_cpp, nsp_command, &ret_val);
	if (err < 0)
		return err;
	ret_val = FIELD_GET(NSP_COMMAND_OPTION, ret_val);

	err = FIELD_GET(NSP_STATUS_RESULT, reg);
	if (err) {
		nfp_warn(cpp, "Result (error) code set: %d (%d) command: %d\n",
			 -err, (int)ret_val, arg->code);
		if (arg->error_cb)
			arg->error_cb(state, ret_val);
		else
			nfp_nsp_print_extended_error(state, ret_val);
		return -err;
	}

	return ret_val;
}

static int nfp_nsp_command(struct nfp_nsp *state, u16 code)
{
	const struct nfp_nsp_command_arg arg = {
		.code		= code,
	};

	return __nfp_nsp_command(state, &arg);
}

static int
nfp_nsp_command_buf_def(struct nfp_nsp *nsp,
			struct nfp_nsp_command_buf_arg *arg)
{
	struct nfp_cpp *cpp = nsp->cpp;
	u64 reg, cpp_buf;
	int err, ret;
	u32 cpp_id;

	err = nfp_cpp_readq(cpp, nfp_resource_cpp_id(nsp->res),
			    nfp_resource_address(nsp->res) +
			    NSP_DFLT_BUFFER,
			    &reg);
	if (err < 0)
		return err;

	cpp_id = FIELD_GET(NSP_DFLT_BUFFER_CPP, reg) << 8;
	cpp_buf = FIELD_GET(NSP_DFLT_BUFFER_ADDRESS, reg);

	if (arg->in_buf && arg->in_size) {
		err = nfp_cpp_write(cpp, cpp_id, cpp_buf,
				    arg->in_buf, arg->in_size);
		if (err < 0)
			return err;
	}
	/* Zero out remaining part of the buffer */
	if (arg->out_buf && arg->out_size && arg->out_size > arg->in_size) {
		err = nfp_cpp_write(cpp, cpp_id, cpp_buf + arg->in_size,
				    arg->out_buf, arg->out_size - arg->in_size);
		if (err < 0)
			return err;
	}

	if (!FIELD_FIT(NSP_BUFFER_CPP, cpp_id >> 8) ||
	    !FIELD_FIT(NSP_BUFFER_ADDRESS, cpp_buf)) {
		nfp_err(cpp, "Buffer out of reach %08x %016llx\n",
			cpp_id, cpp_buf);
		return -EINVAL;
	}

	arg->arg.buf = FIELD_PREP(NSP_BUFFER_CPP, cpp_id >> 8) |
		       FIELD_PREP(NSP_BUFFER_ADDRESS, cpp_buf);
	ret = __nfp_nsp_command(nsp, &arg->arg);
	if (ret < 0)
		return ret;

	if (arg->out_buf && arg->out_size) {
		err = nfp_cpp_read(cpp, cpp_id, cpp_buf,
				   arg->out_buf, arg->out_size);
		if (err < 0)
			return err;
	}

	return ret;
}

static int
nfp_nsp_command_buf_dma_sg(struct nfp_nsp *nsp,
			   struct nfp_nsp_command_buf_arg *arg,
			   unsigned int max_size, unsigned int chunk_order,
			   unsigned int dma_order)
{
	struct nfp_cpp *cpp = nsp->cpp;
	struct nfp_nsp_dma_buf *desc;
	struct {
		dma_addr_t dma_addr;
		unsigned long len;
		void *chunk;
	} *chunks;
	size_t chunk_size, dma_size;
	dma_addr_t dma_desc;
	struct device *dev;
	unsigned long off;
	int i, ret, nseg;
	size_t desc_sz;

	chunk_size = BIT_ULL(chunk_order);
	dma_size = BIT_ULL(dma_order);
	nseg = DIV_ROUND_UP(max_size, chunk_size);

	chunks = kzalloc(array_size(sizeof(*chunks), nseg), GFP_KERNEL);
	if (!chunks)
		return -ENOMEM;

	off = 0;
	ret = -ENOMEM;
	for (i = 0; i < nseg; i++) {
		unsigned long coff;

		chunks[i].chunk = kmalloc(chunk_size,
					  GFP_KERNEL | __GFP_NOWARN);
		if (!chunks[i].chunk)
			goto exit_free_prev;

		chunks[i].len = min_t(u64, chunk_size, max_size - off);

		coff = 0;
		if (arg->in_size > off) {
			coff = min_t(u64, arg->in_size - off, chunk_size);
			memcpy(chunks[i].chunk, arg->in_buf + off, coff);
		}
		memset(chunks[i].chunk + coff, 0, chunk_size - coff);

		off += chunks[i].len;
	}

	dev = nfp_cpp_device(cpp)->parent;

	for (i = 0; i < nseg; i++) {
		dma_addr_t addr;

		addr = dma_map_single(dev, chunks[i].chunk, chunks[i].len,
				      DMA_BIDIRECTIONAL);
		chunks[i].dma_addr = addr;

		ret = dma_mapping_error(dev, addr);
		if (ret)
			goto exit_unmap_prev;

		if (WARN_ONCE(round_down(addr, dma_size) !=
			      round_down(addr + chunks[i].len - 1, dma_size),
			      "unaligned DMA address: %pad %lu %zd\n",
			      &addr, chunks[i].len, dma_size)) {
			ret = -EFAULT;
			i++;
			goto exit_unmap_prev;
		}
	}

	desc_sz = struct_size(desc, descs, nseg);
	desc = kmalloc(desc_sz, GFP_KERNEL);
	if (!desc) {
		ret = -ENOMEM;
		goto exit_unmap_all;
	}

	desc->chunk_cnt = cpu_to_le32(nseg);
	for (i = 0; i < nseg; i++) {
		desc->descs[i].size = cpu_to_le32(chunks[i].len);
		desc->descs[i].addr = cpu_to_le64(chunks[i].dma_addr);
	}

	dma_desc = dma_map_single(dev, desc, desc_sz, DMA_TO_DEVICE);
	ret = dma_mapping_error(dev, dma_desc);
	if (ret)
		goto exit_free_desc;

	arg->arg.dma = true;
	arg->arg.buf = dma_desc;
	ret = __nfp_nsp_command(nsp, &arg->arg);
	if (ret < 0)
		goto exit_unmap_desc;

	i = 0;
	off = 0;
	while (off < arg->out_size) {
		unsigned int len;

		len = min_t(u64, chunks[i].len, arg->out_size - off);
		memcpy(arg->out_buf + off, chunks[i].chunk, len);
		off += len;
		i++;
	}

exit_unmap_desc:
	dma_unmap_single(dev, dma_desc, desc_sz, DMA_TO_DEVICE);
exit_free_desc:
	kfree(desc);
exit_unmap_all:
	i = nseg;
exit_unmap_prev:
	while (--i >= 0)
		dma_unmap_single(dev, chunks[i].dma_addr, chunks[i].len,
				 DMA_BIDIRECTIONAL);
	i = nseg;
exit_free_prev:
	while (--i >= 0)
		kfree(chunks[i].chunk);
	kfree(chunks);
	if (ret < 0)
		nfp_err(cpp, "NSP: SG DMA failed for command 0x%04x: %d (sz:%d cord:%d)\n",
			arg->arg.code, ret, max_size, chunk_order);
	return ret;
}

static int
nfp_nsp_command_buf_dma(struct nfp_nsp *nsp,
			struct nfp_nsp_command_buf_arg *arg,
			unsigned int max_size, unsigned int dma_order)
{
	unsigned int chunk_order, buf_order;
	struct nfp_cpp *cpp = nsp->cpp;
	bool sg_ok;
	u64 reg;
	int err;

	buf_order = order_base_2(roundup_pow_of_two(max_size));

	err = nfp_cpp_readq(cpp, nfp_resource_cpp_id(nsp->res),
			    nfp_resource_address(nsp->res) + NFP_CAP_CMD_DMA_SG,
			    &reg);
	if (err < 0)
		return err;
	sg_ok = reg & BIT_ULL(arg->arg.code - 1);

	if (!sg_ok) {
		if (buf_order > dma_order) {
			nfp_err(cpp, "NSP: can't service non-SG DMA for command 0x%04x\n",
				arg->arg.code);
			return -ENOMEM;
		}
		chunk_order = buf_order;
	} else {
		chunk_order = min_t(unsigned int, dma_order, PAGE_SHIFT);
	}

	return nfp_nsp_command_buf_dma_sg(nsp, arg, max_size, chunk_order,
					  dma_order);
}

static int
nfp_nsp_command_buf(struct nfp_nsp *nsp, struct nfp_nsp_command_buf_arg *arg)
{
	unsigned int dma_order, def_size, max_size;
	struct nfp_cpp *cpp = nsp->cpp;
	u64 reg;
	int err;

	if (nsp->ver.minor < 13) {
		nfp_err(cpp, "NSP: Code 0x%04x with buffer not supported (ABI %hu.%hu)\n",
			arg->arg.code, nsp->ver.major, nsp->ver.minor);
		return -EOPNOTSUPP;
	}

	err = nfp_cpp_readq(cpp, nfp_resource_cpp_id(nsp->res),
			    nfp_resource_address(nsp->res) +
			    NSP_DFLT_BUFFER_CONFIG,
			    &reg);
	if (err < 0)
		return err;

	/* Zero out undefined part of the out buffer */
	if (arg->out_buf && arg->out_size && arg->out_size > arg->in_size)
		memset(arg->out_buf, 0, arg->out_size - arg->in_size);

	max_size = max(arg->in_size, arg->out_size);
	def_size = FIELD_GET(NSP_DFLT_BUFFER_SIZE_MB, reg) * SZ_1M +
		   FIELD_GET(NSP_DFLT_BUFFER_SIZE_4KB, reg) * SZ_4K;
	dma_order = FIELD_GET(NSP_DFLT_BUFFER_DMA_CHUNK_ORDER, reg);
	if (def_size >= max_size) {
		return nfp_nsp_command_buf_def(nsp, arg);
	} else if (!dma_order) {
		nfp_err(cpp, "NSP: default buffer too small for command 0x%04x (%u < %u)\n",
			arg->arg.code, def_size, max_size);
		return -EINVAL;
	}

	return nfp_nsp_command_buf_dma(nsp, arg, max_size, dma_order);
}

int nfp_nsp_wait(struct nfp_nsp *state)
{
	const unsigned long wait_until = jiffies + NFP_NSP_TIMEOUT_BOOT * HZ;
	int err;

	nfp_dbg(state->cpp, "Waiting for NSP to respond (%u sec max).\n",
		NFP_NSP_TIMEOUT_BOOT);

	for (;;) {
		const unsigned long start_time = jiffies;

		err = nfp_nsp_command(state, SPCODE_NOOP);
		if (err != -EAGAIN)
			break;

		if (msleep_interruptible(25)) {
			err = -ERESTARTSYS;
			break;
		}

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
	return nfp_nsp_command(state, SPCODE_SOFT_RESET);
}

int nfp_nsp_mac_reinit(struct nfp_nsp *state)
{
	return nfp_nsp_command(state, SPCODE_MAC_INIT);
}

static void nfp_nsp_load_fw_extended_msg(struct nfp_nsp *state, u32 ret_val)
{
	static const char * const major_msg[] = {
		/* 0 */ "Firmware from driver loaded",
		/* 1 */ "Firmware from flash loaded",
		/* 2 */ "Firmware loading failure",
	};
	static const char * const minor_msg[] = {
		/*  0 */ "",
		/*  1 */ "no named partition on flash",
		/*  2 */ "error reading from flash",
		/*  3 */ "can not deflate",
		/*  4 */ "not a trusted file",
		/*  5 */ "can not parse FW file",
		/*  6 */ "MIP not found in FW file",
		/*  7 */ "null firmware name in MIP",
		/*  8 */ "FW version none",
		/*  9 */ "FW build number none",
		/* 10 */ "no FW selection policy HWInfo key found",
		/* 11 */ "static FW selection policy",
		/* 12 */ "FW version has precedence",
		/* 13 */ "different FW application load requested",
		/* 14 */ "development build",
	};
	unsigned int major, minor;
	const char *level;

	major = FIELD_GET(NFP_FW_LOAD_RET_MAJOR, ret_val);
	minor = FIELD_GET(NFP_FW_LOAD_RET_MINOR, ret_val);

	if (!nfp_nsp_has_stored_fw_load(state))
		return;

	/* Lower the message level in legacy case */
	if (major == 0 && (minor == 0 || minor == 10))
		level = KERN_DEBUG;
	else if (major == 2)
		level = KERN_ERR;
	else
		level = KERN_INFO;

	if (major >= ARRAY_SIZE(major_msg))
		nfp_printk(level, state->cpp, "FW loading status: %x\n",
			   ret_val);
	else if (minor >= ARRAY_SIZE(minor_msg))
		nfp_printk(level, state->cpp, "%s, reason code: %d\n",
			   major_msg[major], minor);
	else
		nfp_printk(level, state->cpp, "%s%c %s\n",
			   major_msg[major], minor ? ',' : '.',
			   minor_msg[minor]);
}

int nfp_nsp_load_fw(struct nfp_nsp *state, const struct firmware *fw)
{
	struct nfp_nsp_command_buf_arg load_fw = {
		{
			.code		= SPCODE_FW_LOAD,
			.option		= fw->size,
			.error_cb	= nfp_nsp_load_fw_extended_msg,
		},
		.in_buf		= fw->data,
		.in_size	= fw->size,
	};
	int ret;

	ret = nfp_nsp_command_buf(state, &load_fw);
	if (ret < 0)
		return ret;

	nfp_nsp_load_fw_extended_msg(state, ret);
	return 0;
}

int nfp_nsp_write_flash(struct nfp_nsp *state, const struct firmware *fw)
{
	struct nfp_nsp_command_buf_arg write_flash = {
		{
			.code		= SPCODE_NSP_WRITE_FLASH,
			.option		= fw->size,
			.timeout_sec	= 900,
		},
		.in_buf		= fw->data,
		.in_size	= fw->size,
	};

	return nfp_nsp_command_buf(state, &write_flash);
}

int nfp_nsp_read_eth_table(struct nfp_nsp *state, void *buf, unsigned int size)
{
	struct nfp_nsp_command_buf_arg eth_rescan = {
		{
			.code		= SPCODE_ETH_RESCAN,
			.option		= size,
		},
		.out_buf	= buf,
		.out_size	= size,
	};

	return nfp_nsp_command_buf(state, &eth_rescan);
}

int nfp_nsp_write_eth_table(struct nfp_nsp *state,
			    const void *buf, unsigned int size)
{
	struct nfp_nsp_command_buf_arg eth_ctrl = {
		{
			.code		= SPCODE_ETH_CONTROL,
			.option		= size,
		},
		.in_buf		= buf,
		.in_size	= size,
	};

	return nfp_nsp_command_buf(state, &eth_ctrl);
}

int nfp_nsp_read_identify(struct nfp_nsp *state, void *buf, unsigned int size)
{
	struct nfp_nsp_command_buf_arg identify = {
		{
			.code		= SPCODE_NSP_IDENTIFY,
			.option		= size,
		},
		.out_buf	= buf,
		.out_size	= size,
	};

	return nfp_nsp_command_buf(state, &identify);
}

int nfp_nsp_read_sensors(struct nfp_nsp *state, unsigned int sensor_mask,
			 void *buf, unsigned int size)
{
	struct nfp_nsp_command_buf_arg sensors = {
		{
			.code		= SPCODE_NSP_SENSORS,
			.option		= sensor_mask,
		},
		.out_buf	= buf,
		.out_size	= size,
	};

	return nfp_nsp_command_buf(state, &sensors);
}

int nfp_nsp_load_stored_fw(struct nfp_nsp *state)
{
	const struct nfp_nsp_command_arg arg = {
		.code		= SPCODE_FW_STORED,
		.error_cb	= nfp_nsp_load_fw_extended_msg,
	};
	int ret;

	ret = __nfp_nsp_command(state, &arg);
	if (ret < 0)
		return ret;

	nfp_nsp_load_fw_extended_msg(state, ret);
	return 0;
}

static int
__nfp_nsp_hwinfo_lookup(struct nfp_nsp *state, void *buf, unsigned int size)
{
	struct nfp_nsp_command_buf_arg hwinfo_lookup = {
		{
			.code		= SPCODE_HWINFO_LOOKUP,
			.option		= size,
		},
		.in_buf		= buf,
		.in_size	= size,
		.out_buf	= buf,
		.out_size	= size,
	};

	return nfp_nsp_command_buf(state, &hwinfo_lookup);
}

int nfp_nsp_hwinfo_lookup(struct nfp_nsp *state, void *buf, unsigned int size)
{
	int err;

	size = min_t(u32, size, NFP_HWINFO_LOOKUP_SIZE);

	err = __nfp_nsp_hwinfo_lookup(state, buf, size);
	if (err)
		return err;

	if (strnlen(buf, size) == size) {
		nfp_err(state->cpp, "NSP HWinfo value not NULL-terminated\n");
		return -EINVAL;
	}

	return 0;
}

int nfp_nsp_versions(struct nfp_nsp *state, void *buf, unsigned int size)
{
	struct nfp_nsp_command_buf_arg versions = {
		{
			.code		= SPCODE_VERSIONS,
			.option		= min_t(u32, size, NFP_VERSIONS_SIZE),
		},
		.out_buf	= buf,
		.out_size	= min_t(u32, size, NFP_VERSIONS_SIZE),
	};

	return nfp_nsp_command_buf(state, &versions);
}

const char *nfp_nsp_versions_get(enum nfp_nsp_versions id, bool flash,
				 const u8 *buf, unsigned int size)
{
	static const u32 id2off[] = {
		[NFP_VERSIONS_BSP] =	NFP_VERSIONS_BSP_OFF,
		[NFP_VERSIONS_CPLD] =	NFP_VERSIONS_CPLD_OFF,
		[NFP_VERSIONS_APP] =	NFP_VERSIONS_APP_OFF,
		[NFP_VERSIONS_BUNDLE] =	NFP_VERSIONS_BUNDLE_OFF,
		[NFP_VERSIONS_UNDI] =	NFP_VERSIONS_UNDI_OFF,
		[NFP_VERSIONS_NCSI] =	NFP_VERSIONS_NCSI_OFF,
		[NFP_VERSIONS_CFGR] =	NFP_VERSIONS_CFGR_OFF,
	};
	unsigned int field, buf_field_cnt, buf_off;

	if (id >= ARRAY_SIZE(id2off) || !id2off[id])
		return ERR_PTR(-EINVAL);

	field = id * 2 + flash;

	buf_field_cnt = get_unaligned_le16(buf);
	if (buf_field_cnt <= field)
		return ERR_PTR(-ENOENT);

	buf_off = get_unaligned_le16(buf + id2off[id] + flash * 2);
	if (!buf_off)
		return ERR_PTR(-ENOENT);

	if (buf_off >= size)
		return ERR_PTR(-EINVAL);
	if (strnlen(&buf[buf_off], size - buf_off) == size - buf_off)
		return ERR_PTR(-EINVAL);

	return (const char *)&buf[buf_off];
}

static int
__nfp_nsp_module_eeprom(struct nfp_nsp *state, void *buf, unsigned int size)
{
	struct nfp_nsp_command_buf_arg module_eeprom = {
		{
			.code		= SPCODE_READ_SFF_EEPROM,
			.option		= size,
		},
		.in_buf		= buf,
		.in_size	= size,
		.out_buf	= buf,
		.out_size	= size,
	};

	return nfp_nsp_command_buf(state, &module_eeprom);
}

int nfp_nsp_read_module_eeprom(struct nfp_nsp *state, int eth_index,
			       unsigned int offset, void *data,
			       unsigned int len, unsigned int *read_len)
{
	struct eeprom_buf {
		u8 metalen;
		__le16 length;
		__le16 offset;
		__le16 readlen;
		u8 eth_index;
		u8 data[0];
	} __packed *buf;
	int bufsz, ret;

	BUILD_BUG_ON(offsetof(struct eeprom_buf, data) % 8);

	/* Buffer must be large enough and rounded to the next block size. */
	bufsz = struct_size(buf, data, round_up(len, NSP_SFF_EEPROM_BLOCK_LEN));
	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	buf->metalen =
		offsetof(struct eeprom_buf, data) / NSP_SFF_EEPROM_BLOCK_LEN;
	buf->length = cpu_to_le16(len);
	buf->offset = cpu_to_le16(offset);
	buf->eth_index = eth_index;

	ret = __nfp_nsp_module_eeprom(state, buf, bufsz);

	*read_len = min_t(unsigned int, len, le16_to_cpu(buf->readlen));
	if (*read_len)
		memcpy(data, buf->data, *read_len);

	if (!ret && *read_len < len)
		ret = -EIO;

	kfree(buf);

	return ret;
}
