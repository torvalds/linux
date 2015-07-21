/*
 * This file is part of the Chelsio T4 Ethernet driver for Linux.
 *
 * Copyright (c) 2003-2014 Chelsio Communications, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
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

#include <linux/delay.h>
#include "cxgb4.h"
#include "t4_regs.h"
#include "t4_values.h"
#include "t4fw_api.h"

/**
 *	t4_wait_op_done_val - wait until an operation is completed
 *	@adapter: the adapter performing the operation
 *	@reg: the register to check for completion
 *	@mask: a single-bit field within @reg that indicates completion
 *	@polarity: the value of the field when the operation is completed
 *	@attempts: number of check iterations
 *	@delay: delay in usecs between iterations
 *	@valp: where to store the value of the register at completion time
 *
 *	Wait until an operation is completed by checking a bit in a register
 *	up to @attempts times.  If @valp is not NULL the value of the register
 *	at the time it indicated completion is stored there.  Returns 0 if the
 *	operation completes and	-EAGAIN	otherwise.
 */
static int t4_wait_op_done_val(struct adapter *adapter, int reg, u32 mask,
			       int polarity, int attempts, int delay, u32 *valp)
{
	while (1) {
		u32 val = t4_read_reg(adapter, reg);

		if (!!(val & mask) == polarity) {
			if (valp)
				*valp = val;
			return 0;
		}
		if (--attempts == 0)
			return -EAGAIN;
		if (delay)
			udelay(delay);
	}
}

static inline int t4_wait_op_done(struct adapter *adapter, int reg, u32 mask,
				  int polarity, int attempts, int delay)
{
	return t4_wait_op_done_val(adapter, reg, mask, polarity, attempts,
				   delay, NULL);
}

/**
 *	t4_set_reg_field - set a register field to a value
 *	@adapter: the adapter to program
 *	@addr: the register address
 *	@mask: specifies the portion of the register to modify
 *	@val: the new value for the register field
 *
 *	Sets a register field specified by the supplied mask to the
 *	given value.
 */
void t4_set_reg_field(struct adapter *adapter, unsigned int addr, u32 mask,
		      u32 val)
{
	u32 v = t4_read_reg(adapter, addr) & ~mask;

	t4_write_reg(adapter, addr, v | val);
	(void) t4_read_reg(adapter, addr);      /* flush */
}

/**
 *	t4_read_indirect - read indirectly addressed registers
 *	@adap: the adapter
 *	@addr_reg: register holding the indirect address
 *	@data_reg: register holding the value of the indirect register
 *	@vals: where the read register values are stored
 *	@nregs: how many indirect registers to read
 *	@start_idx: index of first indirect register to read
 *
 *	Reads registers that are accessed indirectly through an address/data
 *	register pair.
 */
void t4_read_indirect(struct adapter *adap, unsigned int addr_reg,
			     unsigned int data_reg, u32 *vals,
			     unsigned int nregs, unsigned int start_idx)
{
	while (nregs--) {
		t4_write_reg(adap, addr_reg, start_idx);
		*vals++ = t4_read_reg(adap, data_reg);
		start_idx++;
	}
}

/**
 *	t4_write_indirect - write indirectly addressed registers
 *	@adap: the adapter
 *	@addr_reg: register holding the indirect addresses
 *	@data_reg: register holding the value for the indirect registers
 *	@vals: values to write
 *	@nregs: how many indirect registers to write
 *	@start_idx: address of first indirect register to write
 *
 *	Writes a sequential block of registers that are accessed indirectly
 *	through an address/data register pair.
 */
void t4_write_indirect(struct adapter *adap, unsigned int addr_reg,
		       unsigned int data_reg, const u32 *vals,
		       unsigned int nregs, unsigned int start_idx)
{
	while (nregs--) {
		t4_write_reg(adap, addr_reg, start_idx++);
		t4_write_reg(adap, data_reg, *vals++);
	}
}

/*
 * Read a 32-bit PCI Configuration Space register via the PCI-E backdoor
 * mechanism.  This guarantees that we get the real value even if we're
 * operating within a Virtual Machine and the Hypervisor is trapping our
 * Configuration Space accesses.
 */
void t4_hw_pci_read_cfg4(struct adapter *adap, int reg, u32 *val)
{
	u32 req = FUNCTION_V(adap->pf) | REGISTER_V(reg);

	if (CHELSIO_CHIP_VERSION(adap->params.chip) <= CHELSIO_T5)
		req |= ENABLE_F;
	else
		req |= T6_ENABLE_F;

	if (is_t4(adap->params.chip))
		req |= LOCALCFG_F;

	t4_write_reg(adap, PCIE_CFG_SPACE_REQ_A, req);
	*val = t4_read_reg(adap, PCIE_CFG_SPACE_DATA_A);

	/* Reset ENABLE to 0 so reads of PCIE_CFG_SPACE_DATA won't cause a
	 * Configuration Space read.  (None of the other fields matter when
	 * ENABLE is 0 so a simple register write is easier than a
	 * read-modify-write via t4_set_reg_field().)
	 */
	t4_write_reg(adap, PCIE_CFG_SPACE_REQ_A, 0);
}

/*
 * t4_report_fw_error - report firmware error
 * @adap: the adapter
 *
 * The adapter firmware can indicate error conditions to the host.
 * If the firmware has indicated an error, print out the reason for
 * the firmware error.
 */
static void t4_report_fw_error(struct adapter *adap)
{
	static const char *const reason[] = {
		"Crash",                        /* PCIE_FW_EVAL_CRASH */
		"During Device Preparation",    /* PCIE_FW_EVAL_PREP */
		"During Device Configuration",  /* PCIE_FW_EVAL_CONF */
		"During Device Initialization", /* PCIE_FW_EVAL_INIT */
		"Unexpected Event",             /* PCIE_FW_EVAL_UNEXPECTEDEVENT */
		"Insufficient Airflow",         /* PCIE_FW_EVAL_OVERHEAT */
		"Device Shutdown",              /* PCIE_FW_EVAL_DEVICESHUTDOWN */
		"Reserved",                     /* reserved */
	};
	u32 pcie_fw;

	pcie_fw = t4_read_reg(adap, PCIE_FW_A);
	if (pcie_fw & PCIE_FW_ERR_F)
		dev_err(adap->pdev_dev, "Firmware reports adapter error: %s\n",
			reason[PCIE_FW_EVAL_G(pcie_fw)]);
}

/*
 * Get the reply to a mailbox command and store it in @rpl in big-endian order.
 */
static void get_mbox_rpl(struct adapter *adap, __be64 *rpl, int nflit,
			 u32 mbox_addr)
{
	for ( ; nflit; nflit--, mbox_addr += 8)
		*rpl++ = cpu_to_be64(t4_read_reg64(adap, mbox_addr));
}

/*
 * Handle a FW assertion reported in a mailbox.
 */
static void fw_asrt(struct adapter *adap, u32 mbox_addr)
{
	struct fw_debug_cmd asrt;

	get_mbox_rpl(adap, (__be64 *)&asrt, sizeof(asrt) / 8, mbox_addr);
	dev_alert(adap->pdev_dev,
		  "FW assertion at %.16s:%u, val0 %#x, val1 %#x\n",
		  asrt.u.assert.filename_0_7, be32_to_cpu(asrt.u.assert.line),
		  be32_to_cpu(asrt.u.assert.x), be32_to_cpu(asrt.u.assert.y));
}

static void dump_mbox(struct adapter *adap, int mbox, u32 data_reg)
{
	dev_err(adap->pdev_dev,
		"mbox %d: %llx %llx %llx %llx %llx %llx %llx %llx\n", mbox,
		(unsigned long long)t4_read_reg64(adap, data_reg),
		(unsigned long long)t4_read_reg64(adap, data_reg + 8),
		(unsigned long long)t4_read_reg64(adap, data_reg + 16),
		(unsigned long long)t4_read_reg64(adap, data_reg + 24),
		(unsigned long long)t4_read_reg64(adap, data_reg + 32),
		(unsigned long long)t4_read_reg64(adap, data_reg + 40),
		(unsigned long long)t4_read_reg64(adap, data_reg + 48),
		(unsigned long long)t4_read_reg64(adap, data_reg + 56));
}

/**
 *	t4_wr_mbox_meat_timeout - send a command to FW through the given mailbox
 *	@adap: the adapter
 *	@mbox: index of the mailbox to use
 *	@cmd: the command to write
 *	@size: command length in bytes
 *	@rpl: where to optionally store the reply
 *	@sleep_ok: if true we may sleep while awaiting command completion
 *	@timeout: time to wait for command to finish before timing out
 *
 *	Sends the given command to FW through the selected mailbox and waits
 *	for the FW to execute the command.  If @rpl is not %NULL it is used to
 *	store the FW's reply to the command.  The command and its optional
 *	reply are of the same length.  FW can take up to %FW_CMD_MAX_TIMEOUT ms
 *	to respond.  @sleep_ok determines whether we may sleep while awaiting
 *	the response.  If sleeping is allowed we use progressive backoff
 *	otherwise we spin.
 *
 *	The return value is 0 on success or a negative errno on failure.  A
 *	failure can happen either because we are not able to execute the
 *	command or FW executes it but signals an error.  In the latter case
 *	the return value is the error code indicated by FW (negated).
 */
int t4_wr_mbox_meat_timeout(struct adapter *adap, int mbox, const void *cmd,
			    int size, void *rpl, bool sleep_ok, int timeout)
{
	static const int delay[] = {
		1, 1, 3, 5, 10, 10, 20, 50, 100, 200
	};

	u32 v;
	u64 res;
	int i, ms, delay_idx;
	const __be64 *p = cmd;
	u32 data_reg = PF_REG(mbox, CIM_PF_MAILBOX_DATA_A);
	u32 ctl_reg = PF_REG(mbox, CIM_PF_MAILBOX_CTRL_A);

	if ((size & 15) || size > MBOX_LEN)
		return -EINVAL;

	/*
	 * If the device is off-line, as in EEH, commands will time out.
	 * Fail them early so we don't waste time waiting.
	 */
	if (adap->pdev->error_state != pci_channel_io_normal)
		return -EIO;

	v = MBOWNER_G(t4_read_reg(adap, ctl_reg));
	for (i = 0; v == MBOX_OWNER_NONE && i < 3; i++)
		v = MBOWNER_G(t4_read_reg(adap, ctl_reg));

	if (v != MBOX_OWNER_DRV)
		return v ? -EBUSY : -ETIMEDOUT;

	for (i = 0; i < size; i += 8)
		t4_write_reg64(adap, data_reg + i, be64_to_cpu(*p++));

	t4_write_reg(adap, ctl_reg, MBMSGVALID_F | MBOWNER_V(MBOX_OWNER_FW));
	t4_read_reg(adap, ctl_reg);          /* flush write */

	delay_idx = 0;
	ms = delay[0];

	for (i = 0; i < timeout; i += ms) {
		if (sleep_ok) {
			ms = delay[delay_idx];  /* last element may repeat */
			if (delay_idx < ARRAY_SIZE(delay) - 1)
				delay_idx++;
			msleep(ms);
		} else
			mdelay(ms);

		v = t4_read_reg(adap, ctl_reg);
		if (MBOWNER_G(v) == MBOX_OWNER_DRV) {
			if (!(v & MBMSGVALID_F)) {
				t4_write_reg(adap, ctl_reg, 0);
				continue;
			}

			res = t4_read_reg64(adap, data_reg);
			if (FW_CMD_OP_G(res >> 32) == FW_DEBUG_CMD) {
				fw_asrt(adap, data_reg);
				res = FW_CMD_RETVAL_V(EIO);
			} else if (rpl) {
				get_mbox_rpl(adap, rpl, size / 8, data_reg);
			}

			if (FW_CMD_RETVAL_G((int)res))
				dump_mbox(adap, mbox, data_reg);
			t4_write_reg(adap, ctl_reg, 0);
			return -FW_CMD_RETVAL_G((int)res);
		}
	}

	dump_mbox(adap, mbox, data_reg);
	dev_err(adap->pdev_dev, "command %#x in mailbox %d timed out\n",
		*(const u8 *)cmd, mbox);
	t4_report_fw_error(adap);
	return -ETIMEDOUT;
}

int t4_wr_mbox_meat(struct adapter *adap, int mbox, const void *cmd, int size,
		    void *rpl, bool sleep_ok)
{
	return t4_wr_mbox_meat_timeout(adap, mbox, cmd, size, rpl, sleep_ok,
				       FW_CMD_MAX_TIMEOUT);
}

/**
 *	t4_memory_rw - read/write EDC 0, EDC 1 or MC via PCIE memory window
 *	@adap: the adapter
 *	@win: PCI-E Memory Window to use
 *	@mtype: memory type: MEM_EDC0, MEM_EDC1 or MEM_MC
 *	@addr: address within indicated memory type
 *	@len: amount of memory to transfer
 *	@hbuf: host memory buffer
 *	@dir: direction of transfer T4_MEMORY_READ (1) or T4_MEMORY_WRITE (0)
 *
 *	Reads/writes an [almost] arbitrary memory region in the firmware: the
 *	firmware memory address and host buffer must be aligned on 32-bit
 *	boudaries; the length may be arbitrary.  The memory is transferred as
 *	a raw byte sequence from/to the firmware's memory.  If this memory
 *	contains data structures which contain multi-byte integers, it's the
 *	caller's responsibility to perform appropriate byte order conversions.
 */
int t4_memory_rw(struct adapter *adap, int win, int mtype, u32 addr,
		 u32 len, void *hbuf, int dir)
{
	u32 pos, offset, resid, memoffset;
	u32 edc_size, mc_size, win_pf, mem_reg, mem_aperture, mem_base;
	u32 *buf;

	/* Argument sanity checks ...
	 */
	if (addr & 0x3 || (uintptr_t)hbuf & 0x3)
		return -EINVAL;
	buf = (u32 *)hbuf;

	/* It's convenient to be able to handle lengths which aren't a
	 * multiple of 32-bits because we often end up transferring files to
	 * the firmware.  So we'll handle that by normalizing the length here
	 * and then handling any residual transfer at the end.
	 */
	resid = len & 0x3;
	len -= resid;

	/* Offset into the region of memory which is being accessed
	 * MEM_EDC0 = 0
	 * MEM_EDC1 = 1
	 * MEM_MC   = 2 -- MEM_MC for chips with only 1 memory controller
	 * MEM_MC1  = 3 -- for chips with 2 memory controllers (e.g. T5)
	 */
	edc_size  = EDRAM0_SIZE_G(t4_read_reg(adap, MA_EDRAM0_BAR_A));
	if (mtype != MEM_MC1)
		memoffset = (mtype * (edc_size * 1024 * 1024));
	else {
		mc_size = EXT_MEM0_SIZE_G(t4_read_reg(adap,
						      MA_EXT_MEMORY0_BAR_A));
		memoffset = (MEM_MC0 * edc_size + mc_size) * 1024 * 1024;
	}

	/* Determine the PCIE_MEM_ACCESS_OFFSET */
	addr = addr + memoffset;

	/* Each PCI-E Memory Window is programmed with a window size -- or
	 * "aperture" -- which controls the granularity of its mapping onto
	 * adapter memory.  We need to grab that aperture in order to know
	 * how to use the specified window.  The window is also programmed
	 * with the base address of the Memory Window in BAR0's address
	 * space.  For T4 this is an absolute PCI-E Bus Address.  For T5
	 * the address is relative to BAR0.
	 */
	mem_reg = t4_read_reg(adap,
			      PCIE_MEM_ACCESS_REG(PCIE_MEM_ACCESS_BASE_WIN_A,
						  win));
	mem_aperture = 1 << (WINDOW_G(mem_reg) + WINDOW_SHIFT_X);
	mem_base = PCIEOFST_G(mem_reg) << PCIEOFST_SHIFT_X;
	if (is_t4(adap->params.chip))
		mem_base -= adap->t4_bar0;
	win_pf = is_t4(adap->params.chip) ? 0 : PFNUM_V(adap->pf);

	/* Calculate our initial PCI-E Memory Window Position and Offset into
	 * that Window.
	 */
	pos = addr & ~(mem_aperture-1);
	offset = addr - pos;

	/* Set up initial PCI-E Memory Window to cover the start of our
	 * transfer.  (Read it back to ensure that changes propagate before we
	 * attempt to use the new value.)
	 */
	t4_write_reg(adap,
		     PCIE_MEM_ACCESS_REG(PCIE_MEM_ACCESS_OFFSET_A, win),
		     pos | win_pf);
	t4_read_reg(adap,
		    PCIE_MEM_ACCESS_REG(PCIE_MEM_ACCESS_OFFSET_A, win));

	/* Transfer data to/from the adapter as long as there's an integral
	 * number of 32-bit transfers to complete.
	 *
	 * A note on Endianness issues:
	 *
	 * The "register" reads and writes below from/to the PCI-E Memory
	 * Window invoke the standard adapter Big-Endian to PCI-E Link
	 * Little-Endian "swizzel."  As a result, if we have the following
	 * data in adapter memory:
	 *
	 *     Memory:  ... | b0 | b1 | b2 | b3 | ...
	 *     Address:      i+0  i+1  i+2  i+3
	 *
	 * Then a read of the adapter memory via the PCI-E Memory Window
	 * will yield:
	 *
	 *     x = readl(i)
	 *         31                  0
	 *         [ b3 | b2 | b1 | b0 ]
	 *
	 * If this value is stored into local memory on a Little-Endian system
	 * it will show up correctly in local memory as:
	 *
	 *     ( ..., b0, b1, b2, b3, ... )
	 *
	 * But on a Big-Endian system, the store will show up in memory
	 * incorrectly swizzled as:
	 *
	 *     ( ..., b3, b2, b1, b0, ... )
	 *
	 * So we need to account for this in the reads and writes to the
	 * PCI-E Memory Window below by undoing the register read/write
	 * swizzels.
	 */
	while (len > 0) {
		if (dir == T4_MEMORY_READ)
			*buf++ = le32_to_cpu((__force __le32)t4_read_reg(adap,
						mem_base + offset));
		else
			t4_write_reg(adap, mem_base + offset,
				     (__force u32)cpu_to_le32(*buf++));
		offset += sizeof(__be32);
		len -= sizeof(__be32);

		/* If we've reached the end of our current window aperture,
		 * move the PCI-E Memory Window on to the next.  Note that
		 * doing this here after "len" may be 0 allows us to set up
		 * the PCI-E Memory Window for a possible final residual
		 * transfer below ...
		 */
		if (offset == mem_aperture) {
			pos += mem_aperture;
			offset = 0;
			t4_write_reg(adap,
				PCIE_MEM_ACCESS_REG(PCIE_MEM_ACCESS_OFFSET_A,
						    win), pos | win_pf);
			t4_read_reg(adap,
				PCIE_MEM_ACCESS_REG(PCIE_MEM_ACCESS_OFFSET_A,
						    win));
		}
	}

	/* If the original transfer had a length which wasn't a multiple of
	 * 32-bits, now's where we need to finish off the transfer of the
	 * residual amount.  The PCI-E Memory Window has already been moved
	 * above (if necessary) to cover this final transfer.
	 */
	if (resid) {
		union {
			u32 word;
			char byte[4];
		} last;
		unsigned char *bp;
		int i;

		if (dir == T4_MEMORY_READ) {
			last.word = le32_to_cpu(
					(__force __le32)t4_read_reg(adap,
						mem_base + offset));
			for (bp = (unsigned char *)buf, i = resid; i < 4; i++)
				bp[i] = last.byte[i];
		} else {
			last.word = *buf;
			for (i = resid; i < 4; i++)
				last.byte[i] = 0;
			t4_write_reg(adap, mem_base + offset,
				     (__force u32)cpu_to_le32(last.word));
		}
	}

	return 0;
}

/* Return the specified PCI-E Configuration Space register from our Physical
 * Function.  We try first via a Firmware LDST Command since we prefer to let
 * the firmware own all of these registers, but if that fails we go for it
 * directly ourselves.
 */
u32 t4_read_pcie_cfg4(struct adapter *adap, int reg)
{
	u32 val, ldst_addrspace;

	/* If fw_attach != 0, construct and send the Firmware LDST Command to
	 * retrieve the specified PCI-E Configuration Space register.
	 */
	struct fw_ldst_cmd ldst_cmd;
	int ret;

	memset(&ldst_cmd, 0, sizeof(ldst_cmd));
	ldst_addrspace = FW_LDST_CMD_ADDRSPACE_V(FW_LDST_ADDRSPC_FUNC_PCIE);
	ldst_cmd.op_to_addrspace = cpu_to_be32(FW_CMD_OP_V(FW_LDST_CMD) |
					       FW_CMD_REQUEST_F |
					       FW_CMD_READ_F |
					       ldst_addrspace);
	ldst_cmd.cycles_to_len16 = cpu_to_be32(FW_LEN16(ldst_cmd));
	ldst_cmd.u.pcie.select_naccess = FW_LDST_CMD_NACCESS_V(1);
	ldst_cmd.u.pcie.ctrl_to_fn =
		(FW_LDST_CMD_LC_F | FW_LDST_CMD_FN_V(adap->pf));
	ldst_cmd.u.pcie.r = reg;

	/* If the LDST Command succeeds, return the result, otherwise
	 * fall through to reading it directly ourselves ...
	 */
	ret = t4_wr_mbox(adap, adap->mbox, &ldst_cmd, sizeof(ldst_cmd),
			 &ldst_cmd);
	if (ret == 0)
		val = be32_to_cpu(ldst_cmd.u.pcie.data[0]);
	else
		/* Read the desired Configuration Space register via the PCI-E
		 * Backdoor mechanism.
		 */
		t4_hw_pci_read_cfg4(adap, reg, &val);
	return val;
}

/* Get the window based on base passed to it.
 * Window aperture is currently unhandled, but there is no use case for it
 * right now
 */
static u32 t4_get_window(struct adapter *adap, u32 pci_base, u64 pci_mask,
			 u32 memwin_base)
{
	u32 ret;

	if (is_t4(adap->params.chip)) {
		u32 bar0;

		/* Truncation intentional: we only read the bottom 32-bits of
		 * the 64-bit BAR0/BAR1 ...  We use the hardware backdoor
		 * mechanism to read BAR0 instead of using
		 * pci_resource_start() because we could be operating from
		 * within a Virtual Machine which is trapping our accesses to
		 * our Configuration Space and we need to set up the PCI-E
		 * Memory Window decoders with the actual addresses which will
		 * be coming across the PCI-E link.
		 */
		bar0 = t4_read_pcie_cfg4(adap, pci_base);
		bar0 &= pci_mask;
		adap->t4_bar0 = bar0;

		ret = bar0 + memwin_base;
	} else {
		/* For T5, only relative offset inside the PCIe BAR is passed */
		ret = memwin_base;
	}
	return ret;
}

/* Get the default utility window (win0) used by everyone */
u32 t4_get_util_window(struct adapter *adap)
{
	return t4_get_window(adap, PCI_BASE_ADDRESS_0,
			     PCI_BASE_ADDRESS_MEM_MASK, MEMWIN0_BASE);
}

/* Set up memory window for accessing adapter memory ranges.  (Read
 * back MA register to ensure that changes propagate before we attempt
 * to use the new values.)
 */
void t4_setup_memwin(struct adapter *adap, u32 memwin_base, u32 window)
{
	t4_write_reg(adap,
		     PCIE_MEM_ACCESS_REG(PCIE_MEM_ACCESS_BASE_WIN_A, window),
		     memwin_base | BIR_V(0) |
		     WINDOW_V(ilog2(MEMWIN0_APERTURE) - WINDOW_SHIFT_X));
	t4_read_reg(adap,
		    PCIE_MEM_ACCESS_REG(PCIE_MEM_ACCESS_BASE_WIN_A, window));
}

/**
 *	t4_get_regs_len - return the size of the chips register set
 *	@adapter: the adapter
 *
 *	Returns the size of the chip's BAR0 register space.
 */
unsigned int t4_get_regs_len(struct adapter *adapter)
{
	unsigned int chip_version = CHELSIO_CHIP_VERSION(adapter->params.chip);

	switch (chip_version) {
	case CHELSIO_T4:
		return T4_REGMAP_SIZE;

	case CHELSIO_T5:
	case CHELSIO_T6:
		return T5_REGMAP_SIZE;
	}

	dev_err(adapter->pdev_dev,
		"Unsupported chip version %d\n", chip_version);
	return 0;
}

/**
 *	t4_get_regs - read chip registers into provided buffer
 *	@adap: the adapter
 *	@buf: register buffer
 *	@buf_size: size (in bytes) of register buffer
 *
 *	If the provided register buffer isn't large enough for the chip's
 *	full register range, the register dump will be truncated to the
 *	register buffer's size.
 */
void t4_get_regs(struct adapter *adap, void *buf, size_t buf_size)
{
	static const unsigned int t4_reg_ranges[] = {
		0x1008, 0x1108,
		0x1180, 0x11b4,
		0x11fc, 0x123c,
		0x1300, 0x173c,
		0x1800, 0x18fc,
		0x3000, 0x305c,
		0x3068, 0x30d8,
		0x30e0, 0x5924,
		0x5960, 0x59d4,
		0x5a00, 0x5af8,
		0x6000, 0x6098,
		0x6100, 0x6150,
		0x6200, 0x6208,
		0x6240, 0x6248,
		0x6280, 0x6338,
		0x6370, 0x638c,
		0x6400, 0x643c,
		0x6500, 0x6524,
		0x6a00, 0x6a38,
		0x6a60, 0x6a78,
		0x6b00, 0x6b84,
		0x6bf0, 0x6c84,
		0x6cf0, 0x6d84,
		0x6df0, 0x6e84,
		0x6ef0, 0x6f84,
		0x6ff0, 0x7084,
		0x70f0, 0x7184,
		0x71f0, 0x7284,
		0x72f0, 0x7384,
		0x73f0, 0x7450,
		0x7500, 0x7530,
		0x7600, 0x761c,
		0x7680, 0x76cc,
		0x7700, 0x7798,
		0x77c0, 0x77fc,
		0x7900, 0x79fc,
		0x7b00, 0x7c38,
		0x7d00, 0x7efc,
		0x8dc0, 0x8e1c,
		0x8e30, 0x8e78,
		0x8ea0, 0x8f6c,
		0x8fc0, 0x9074,
		0x90fc, 0x90fc,
		0x9400, 0x9458,
		0x9600, 0x96bc,
		0x9800, 0x9808,
		0x9820, 0x983c,
		0x9850, 0x9864,
		0x9c00, 0x9c6c,
		0x9c80, 0x9cec,
		0x9d00, 0x9d6c,
		0x9d80, 0x9dec,
		0x9e00, 0x9e6c,
		0x9e80, 0x9eec,
		0x9f00, 0x9f6c,
		0x9f80, 0x9fec,
		0xd004, 0xd03c,
		0xdfc0, 0xdfe0,
		0xe000, 0xea7c,
		0xf000, 0x11110,
		0x11118, 0x11190,
		0x19040, 0x1906c,
		0x19078, 0x19080,
		0x1908c, 0x19124,
		0x19150, 0x191b0,
		0x191d0, 0x191e8,
		0x19238, 0x1924c,
		0x193f8, 0x19474,
		0x19490, 0x194f8,
		0x19800, 0x19f4c,
		0x1a000, 0x1a06c,
		0x1a0b0, 0x1a120,
		0x1a128, 0x1a138,
		0x1a190, 0x1a1c4,
		0x1a1fc, 0x1a1fc,
		0x1e040, 0x1e04c,
		0x1e284, 0x1e28c,
		0x1e2c0, 0x1e2c0,
		0x1e2e0, 0x1e2e0,
		0x1e300, 0x1e384,
		0x1e3c0, 0x1e3c8,
		0x1e440, 0x1e44c,
		0x1e684, 0x1e68c,
		0x1e6c0, 0x1e6c0,
		0x1e6e0, 0x1e6e0,
		0x1e700, 0x1e784,
		0x1e7c0, 0x1e7c8,
		0x1e840, 0x1e84c,
		0x1ea84, 0x1ea8c,
		0x1eac0, 0x1eac0,
		0x1eae0, 0x1eae0,
		0x1eb00, 0x1eb84,
		0x1ebc0, 0x1ebc8,
		0x1ec40, 0x1ec4c,
		0x1ee84, 0x1ee8c,
		0x1eec0, 0x1eec0,
		0x1eee0, 0x1eee0,
		0x1ef00, 0x1ef84,
		0x1efc0, 0x1efc8,
		0x1f040, 0x1f04c,
		0x1f284, 0x1f28c,
		0x1f2c0, 0x1f2c0,
		0x1f2e0, 0x1f2e0,
		0x1f300, 0x1f384,
		0x1f3c0, 0x1f3c8,
		0x1f440, 0x1f44c,
		0x1f684, 0x1f68c,
		0x1f6c0, 0x1f6c0,
		0x1f6e0, 0x1f6e0,
		0x1f700, 0x1f784,
		0x1f7c0, 0x1f7c8,
		0x1f840, 0x1f84c,
		0x1fa84, 0x1fa8c,
		0x1fac0, 0x1fac0,
		0x1fae0, 0x1fae0,
		0x1fb00, 0x1fb84,
		0x1fbc0, 0x1fbc8,
		0x1fc40, 0x1fc4c,
		0x1fe84, 0x1fe8c,
		0x1fec0, 0x1fec0,
		0x1fee0, 0x1fee0,
		0x1ff00, 0x1ff84,
		0x1ffc0, 0x1ffc8,
		0x20000, 0x2002c,
		0x20100, 0x2013c,
		0x20190, 0x201c8,
		0x20200, 0x20318,
		0x20400, 0x20528,
		0x20540, 0x20614,
		0x21000, 0x21040,
		0x2104c, 0x21060,
		0x210c0, 0x210ec,
		0x21200, 0x21268,
		0x21270, 0x21284,
		0x212fc, 0x21388,
		0x21400, 0x21404,
		0x21500, 0x21518,
		0x2152c, 0x2153c,
		0x21550, 0x21554,
		0x21600, 0x21600,
		0x21608, 0x21628,
		0x21630, 0x2163c,
		0x21700, 0x2171c,
		0x21780, 0x2178c,
		0x21800, 0x21c38,
		0x21c80, 0x21d7c,
		0x21e00, 0x21e04,
		0x22000, 0x2202c,
		0x22100, 0x2213c,
		0x22190, 0x221c8,
		0x22200, 0x22318,
		0x22400, 0x22528,
		0x22540, 0x22614,
		0x23000, 0x23040,
		0x2304c, 0x23060,
		0x230c0, 0x230ec,
		0x23200, 0x23268,
		0x23270, 0x23284,
		0x232fc, 0x23388,
		0x23400, 0x23404,
		0x23500, 0x23518,
		0x2352c, 0x2353c,
		0x23550, 0x23554,
		0x23600, 0x23600,
		0x23608, 0x23628,
		0x23630, 0x2363c,
		0x23700, 0x2371c,
		0x23780, 0x2378c,
		0x23800, 0x23c38,
		0x23c80, 0x23d7c,
		0x23e00, 0x23e04,
		0x24000, 0x2402c,
		0x24100, 0x2413c,
		0x24190, 0x241c8,
		0x24200, 0x24318,
		0x24400, 0x24528,
		0x24540, 0x24614,
		0x25000, 0x25040,
		0x2504c, 0x25060,
		0x250c0, 0x250ec,
		0x25200, 0x25268,
		0x25270, 0x25284,
		0x252fc, 0x25388,
		0x25400, 0x25404,
		0x25500, 0x25518,
		0x2552c, 0x2553c,
		0x25550, 0x25554,
		0x25600, 0x25600,
		0x25608, 0x25628,
		0x25630, 0x2563c,
		0x25700, 0x2571c,
		0x25780, 0x2578c,
		0x25800, 0x25c38,
		0x25c80, 0x25d7c,
		0x25e00, 0x25e04,
		0x26000, 0x2602c,
		0x26100, 0x2613c,
		0x26190, 0x261c8,
		0x26200, 0x26318,
		0x26400, 0x26528,
		0x26540, 0x26614,
		0x27000, 0x27040,
		0x2704c, 0x27060,
		0x270c0, 0x270ec,
		0x27200, 0x27268,
		0x27270, 0x27284,
		0x272fc, 0x27388,
		0x27400, 0x27404,
		0x27500, 0x27518,
		0x2752c, 0x2753c,
		0x27550, 0x27554,
		0x27600, 0x27600,
		0x27608, 0x27628,
		0x27630, 0x2763c,
		0x27700, 0x2771c,
		0x27780, 0x2778c,
		0x27800, 0x27c38,
		0x27c80, 0x27d7c,
		0x27e00, 0x27e04,
	};

	static const unsigned int t5_reg_ranges[] = {
		0x1008, 0x1148,
		0x1180, 0x11b4,
		0x11fc, 0x123c,
		0x1280, 0x173c,
		0x1800, 0x18fc,
		0x3000, 0x3028,
		0x3068, 0x30d8,
		0x30e0, 0x30fc,
		0x3140, 0x357c,
		0x35a8, 0x35cc,
		0x35ec, 0x35ec,
		0x3600, 0x5624,
		0x56cc, 0x575c,
		0x580c, 0x5814,
		0x5890, 0x58bc,
		0x5940, 0x59dc,
		0x59fc, 0x5a18,
		0x5a60, 0x5a9c,
		0x5b94, 0x5bfc,
		0x6000, 0x6040,
		0x6058, 0x614c,
		0x7700, 0x7798,
		0x77c0, 0x78fc,
		0x7b00, 0x7c54,
		0x7d00, 0x7efc,
		0x8dc0, 0x8de0,
		0x8df8, 0x8e84,
		0x8ea0, 0x8f84,
		0x8fc0, 0x90f8,
		0x9400, 0x9470,
		0x9600, 0x96f4,
		0x9800, 0x9808,
		0x9820, 0x983c,
		0x9850, 0x9864,
		0x9c00, 0x9c6c,
		0x9c80, 0x9cec,
		0x9d00, 0x9d6c,
		0x9d80, 0x9dec,
		0x9e00, 0x9e6c,
		0x9e80, 0x9eec,
		0x9f00, 0x9f6c,
		0x9f80, 0xa020,
		0xd004, 0xd03c,
		0xdfc0, 0xdfe0,
		0xe000, 0x11088,
		0x1109c, 0x11110,
		0x11118, 0x1117c,
		0x11190, 0x11204,
		0x19040, 0x1906c,
		0x19078, 0x19080,
		0x1908c, 0x19124,
		0x19150, 0x191b0,
		0x191d0, 0x191e8,
		0x19238, 0x19290,
		0x193f8, 0x19474,
		0x19490, 0x194cc,
		0x194f0, 0x194f8,
		0x19c00, 0x19c60,
		0x19c94, 0x19e10,
		0x19e50, 0x19f34,
		0x19f40, 0x19f50,
		0x19f90, 0x19fe4,
		0x1a000, 0x1a06c,
		0x1a0b0, 0x1a120,
		0x1a128, 0x1a138,
		0x1a190, 0x1a1c4,
		0x1a1fc, 0x1a1fc,
		0x1e008, 0x1e00c,
		0x1e040, 0x1e04c,
		0x1e284, 0x1e290,
		0x1e2c0, 0x1e2c0,
		0x1e2e0, 0x1e2e0,
		0x1e300, 0x1e384,
		0x1e3c0, 0x1e3c8,
		0x1e408, 0x1e40c,
		0x1e440, 0x1e44c,
		0x1e684, 0x1e690,
		0x1e6c0, 0x1e6c0,
		0x1e6e0, 0x1e6e0,
		0x1e700, 0x1e784,
		0x1e7c0, 0x1e7c8,
		0x1e808, 0x1e80c,
		0x1e840, 0x1e84c,
		0x1ea84, 0x1ea90,
		0x1eac0, 0x1eac0,
		0x1eae0, 0x1eae0,
		0x1eb00, 0x1eb84,
		0x1ebc0, 0x1ebc8,
		0x1ec08, 0x1ec0c,
		0x1ec40, 0x1ec4c,
		0x1ee84, 0x1ee90,
		0x1eec0, 0x1eec0,
		0x1eee0, 0x1eee0,
		0x1ef00, 0x1ef84,
		0x1efc0, 0x1efc8,
		0x1f008, 0x1f00c,
		0x1f040, 0x1f04c,
		0x1f284, 0x1f290,
		0x1f2c0, 0x1f2c0,
		0x1f2e0, 0x1f2e0,
		0x1f300, 0x1f384,
		0x1f3c0, 0x1f3c8,
		0x1f408, 0x1f40c,
		0x1f440, 0x1f44c,
		0x1f684, 0x1f690,
		0x1f6c0, 0x1f6c0,
		0x1f6e0, 0x1f6e0,
		0x1f700, 0x1f784,
		0x1f7c0, 0x1f7c8,
		0x1f808, 0x1f80c,
		0x1f840, 0x1f84c,
		0x1fa84, 0x1fa90,
		0x1fac0, 0x1fac0,
		0x1fae0, 0x1fae0,
		0x1fb00, 0x1fb84,
		0x1fbc0, 0x1fbc8,
		0x1fc08, 0x1fc0c,
		0x1fc40, 0x1fc4c,
		0x1fe84, 0x1fe90,
		0x1fec0, 0x1fec0,
		0x1fee0, 0x1fee0,
		0x1ff00, 0x1ff84,
		0x1ffc0, 0x1ffc8,
		0x30000, 0x30030,
		0x30100, 0x30144,
		0x30190, 0x301d0,
		0x30200, 0x30318,
		0x30400, 0x3052c,
		0x30540, 0x3061c,
		0x30800, 0x30834,
		0x308c0, 0x30908,
		0x30910, 0x309ac,
		0x30a00, 0x30a2c,
		0x30a44, 0x30a50,
		0x30a74, 0x30c24,
		0x30d00, 0x30d00,
		0x30d08, 0x30d14,
		0x30d1c, 0x30d20,
		0x30d3c, 0x30d50,
		0x31200, 0x3120c,
		0x31220, 0x31220,
		0x31240, 0x31240,
		0x31600, 0x3160c,
		0x31a00, 0x31a1c,
		0x31e00, 0x31e20,
		0x31e38, 0x31e3c,
		0x31e80, 0x31e80,
		0x31e88, 0x31ea8,
		0x31eb0, 0x31eb4,
		0x31ec8, 0x31ed4,
		0x31fb8, 0x32004,
		0x32200, 0x32200,
		0x32208, 0x32240,
		0x32248, 0x32280,
		0x32288, 0x322c0,
		0x322c8, 0x322fc,
		0x32600, 0x32630,
		0x32a00, 0x32abc,
		0x32b00, 0x32b70,
		0x33000, 0x33048,
		0x33060, 0x3309c,
		0x330f0, 0x33148,
		0x33160, 0x3319c,
		0x331f0, 0x332e4,
		0x332f8, 0x333e4,
		0x333f8, 0x33448,
		0x33460, 0x3349c,
		0x334f0, 0x33548,
		0x33560, 0x3359c,
		0x335f0, 0x336e4,
		0x336f8, 0x337e4,
		0x337f8, 0x337fc,
		0x33814, 0x33814,
		0x3382c, 0x3382c,
		0x33880, 0x3388c,
		0x338e8, 0x338ec,
		0x33900, 0x33948,
		0x33960, 0x3399c,
		0x339f0, 0x33ae4,
		0x33af8, 0x33b10,
		0x33b28, 0x33b28,
		0x33b3c, 0x33b50,
		0x33bf0, 0x33c10,
		0x33c28, 0x33c28,
		0x33c3c, 0x33c50,
		0x33cf0, 0x33cfc,
		0x34000, 0x34030,
		0x34100, 0x34144,
		0x34190, 0x341d0,
		0x34200, 0x34318,
		0x34400, 0x3452c,
		0x34540, 0x3461c,
		0x34800, 0x34834,
		0x348c0, 0x34908,
		0x34910, 0x349ac,
		0x34a00, 0x34a2c,
		0x34a44, 0x34a50,
		0x34a74, 0x34c24,
		0x34d00, 0x34d00,
		0x34d08, 0x34d14,
		0x34d1c, 0x34d20,
		0x34d3c, 0x34d50,
		0x35200, 0x3520c,
		0x35220, 0x35220,
		0x35240, 0x35240,
		0x35600, 0x3560c,
		0x35a00, 0x35a1c,
		0x35e00, 0x35e20,
		0x35e38, 0x35e3c,
		0x35e80, 0x35e80,
		0x35e88, 0x35ea8,
		0x35eb0, 0x35eb4,
		0x35ec8, 0x35ed4,
		0x35fb8, 0x36004,
		0x36200, 0x36200,
		0x36208, 0x36240,
		0x36248, 0x36280,
		0x36288, 0x362c0,
		0x362c8, 0x362fc,
		0x36600, 0x36630,
		0x36a00, 0x36abc,
		0x36b00, 0x36b70,
		0x37000, 0x37048,
		0x37060, 0x3709c,
		0x370f0, 0x37148,
		0x37160, 0x3719c,
		0x371f0, 0x372e4,
		0x372f8, 0x373e4,
		0x373f8, 0x37448,
		0x37460, 0x3749c,
		0x374f0, 0x37548,
		0x37560, 0x3759c,
		0x375f0, 0x376e4,
		0x376f8, 0x377e4,
		0x377f8, 0x377fc,
		0x37814, 0x37814,
		0x3782c, 0x3782c,
		0x37880, 0x3788c,
		0x378e8, 0x378ec,
		0x37900, 0x37948,
		0x37960, 0x3799c,
		0x379f0, 0x37ae4,
		0x37af8, 0x37b10,
		0x37b28, 0x37b28,
		0x37b3c, 0x37b50,
		0x37bf0, 0x37c10,
		0x37c28, 0x37c28,
		0x37c3c, 0x37c50,
		0x37cf0, 0x37cfc,
		0x38000, 0x38030,
		0x38100, 0x38144,
		0x38190, 0x381d0,
		0x38200, 0x38318,
		0x38400, 0x3852c,
		0x38540, 0x3861c,
		0x38800, 0x38834,
		0x388c0, 0x38908,
		0x38910, 0x389ac,
		0x38a00, 0x38a2c,
		0x38a44, 0x38a50,
		0x38a74, 0x38c24,
		0x38d00, 0x38d00,
		0x38d08, 0x38d14,
		0x38d1c, 0x38d20,
		0x38d3c, 0x38d50,
		0x39200, 0x3920c,
		0x39220, 0x39220,
		0x39240, 0x39240,
		0x39600, 0x3960c,
		0x39a00, 0x39a1c,
		0x39e00, 0x39e20,
		0x39e38, 0x39e3c,
		0x39e80, 0x39e80,
		0x39e88, 0x39ea8,
		0x39eb0, 0x39eb4,
		0x39ec8, 0x39ed4,
		0x39fb8, 0x3a004,
		0x3a200, 0x3a200,
		0x3a208, 0x3a240,
		0x3a248, 0x3a280,
		0x3a288, 0x3a2c0,
		0x3a2c8, 0x3a2fc,
		0x3a600, 0x3a630,
		0x3aa00, 0x3aabc,
		0x3ab00, 0x3ab70,
		0x3b000, 0x3b048,
		0x3b060, 0x3b09c,
		0x3b0f0, 0x3b148,
		0x3b160, 0x3b19c,
		0x3b1f0, 0x3b2e4,
		0x3b2f8, 0x3b3e4,
		0x3b3f8, 0x3b448,
		0x3b460, 0x3b49c,
		0x3b4f0, 0x3b548,
		0x3b560, 0x3b59c,
		0x3b5f0, 0x3b6e4,
		0x3b6f8, 0x3b7e4,
		0x3b7f8, 0x3b7fc,
		0x3b814, 0x3b814,
		0x3b82c, 0x3b82c,
		0x3b880, 0x3b88c,
		0x3b8e8, 0x3b8ec,
		0x3b900, 0x3b948,
		0x3b960, 0x3b99c,
		0x3b9f0, 0x3bae4,
		0x3baf8, 0x3bb10,
		0x3bb28, 0x3bb28,
		0x3bb3c, 0x3bb50,
		0x3bbf0, 0x3bc10,
		0x3bc28, 0x3bc28,
		0x3bc3c, 0x3bc50,
		0x3bcf0, 0x3bcfc,
		0x3c000, 0x3c030,
		0x3c100, 0x3c144,
		0x3c190, 0x3c1d0,
		0x3c200, 0x3c318,
		0x3c400, 0x3c52c,
		0x3c540, 0x3c61c,
		0x3c800, 0x3c834,
		0x3c8c0, 0x3c908,
		0x3c910, 0x3c9ac,
		0x3ca00, 0x3ca2c,
		0x3ca44, 0x3ca50,
		0x3ca74, 0x3cc24,
		0x3cd00, 0x3cd00,
		0x3cd08, 0x3cd14,
		0x3cd1c, 0x3cd20,
		0x3cd3c, 0x3cd50,
		0x3d200, 0x3d20c,
		0x3d220, 0x3d220,
		0x3d240, 0x3d240,
		0x3d600, 0x3d60c,
		0x3da00, 0x3da1c,
		0x3de00, 0x3de20,
		0x3de38, 0x3de3c,
		0x3de80, 0x3de80,
		0x3de88, 0x3dea8,
		0x3deb0, 0x3deb4,
		0x3dec8, 0x3ded4,
		0x3dfb8, 0x3e004,
		0x3e200, 0x3e200,
		0x3e208, 0x3e240,
		0x3e248, 0x3e280,
		0x3e288, 0x3e2c0,
		0x3e2c8, 0x3e2fc,
		0x3e600, 0x3e630,
		0x3ea00, 0x3eabc,
		0x3eb00, 0x3eb70,
		0x3f000, 0x3f048,
		0x3f060, 0x3f09c,
		0x3f0f0, 0x3f148,
		0x3f160, 0x3f19c,
		0x3f1f0, 0x3f2e4,
		0x3f2f8, 0x3f3e4,
		0x3f3f8, 0x3f448,
		0x3f460, 0x3f49c,
		0x3f4f0, 0x3f548,
		0x3f560, 0x3f59c,
		0x3f5f0, 0x3f6e4,
		0x3f6f8, 0x3f7e4,
		0x3f7f8, 0x3f7fc,
		0x3f814, 0x3f814,
		0x3f82c, 0x3f82c,
		0x3f880, 0x3f88c,
		0x3f8e8, 0x3f8ec,
		0x3f900, 0x3f948,
		0x3f960, 0x3f99c,
		0x3f9f0, 0x3fae4,
		0x3faf8, 0x3fb10,
		0x3fb28, 0x3fb28,
		0x3fb3c, 0x3fb50,
		0x3fbf0, 0x3fc10,
		0x3fc28, 0x3fc28,
		0x3fc3c, 0x3fc50,
		0x3fcf0, 0x3fcfc,
		0x40000, 0x4000c,
		0x40040, 0x40068,
		0x4007c, 0x40144,
		0x40180, 0x4018c,
		0x40200, 0x40298,
		0x402ac, 0x4033c,
		0x403f8, 0x403fc,
		0x41304, 0x413c4,
		0x41400, 0x4141c,
		0x41480, 0x414d0,
		0x44000, 0x44078,
		0x440c0, 0x44278,
		0x442c0, 0x44478,
		0x444c0, 0x44678,
		0x446c0, 0x44878,
		0x448c0, 0x449fc,
		0x45000, 0x45068,
		0x45080, 0x45084,
		0x450a0, 0x450b0,
		0x45200, 0x45268,
		0x45280, 0x45284,
		0x452a0, 0x452b0,
		0x460c0, 0x460e4,
		0x47000, 0x4708c,
		0x47200, 0x47250,
		0x47400, 0x47420,
		0x47600, 0x47618,
		0x47800, 0x47814,
		0x48000, 0x4800c,
		0x48040, 0x48068,
		0x4807c, 0x48144,
		0x48180, 0x4818c,
		0x48200, 0x48298,
		0x482ac, 0x4833c,
		0x483f8, 0x483fc,
		0x49304, 0x493c4,
		0x49400, 0x4941c,
		0x49480, 0x494d0,
		0x4c000, 0x4c078,
		0x4c0c0, 0x4c278,
		0x4c2c0, 0x4c478,
		0x4c4c0, 0x4c678,
		0x4c6c0, 0x4c878,
		0x4c8c0, 0x4c9fc,
		0x4d000, 0x4d068,
		0x4d080, 0x4d084,
		0x4d0a0, 0x4d0b0,
		0x4d200, 0x4d268,
		0x4d280, 0x4d284,
		0x4d2a0, 0x4d2b0,
		0x4e0c0, 0x4e0e4,
		0x4f000, 0x4f08c,
		0x4f200, 0x4f250,
		0x4f400, 0x4f420,
		0x4f600, 0x4f618,
		0x4f800, 0x4f814,
		0x50000, 0x500cc,
		0x50400, 0x50400,
		0x50800, 0x508cc,
		0x50c00, 0x50c00,
		0x51000, 0x5101c,
		0x51300, 0x51308,
	};

	static const unsigned int t6_reg_ranges[] = {
		0x1008, 0x114c,
		0x1180, 0x11b4,
		0x11fc, 0x1250,
		0x1280, 0x133c,
		0x1800, 0x18fc,
		0x3000, 0x302c,
		0x3060, 0x30d8,
		0x30e0, 0x30fc,
		0x3140, 0x357c,
		0x35a8, 0x35cc,
		0x35ec, 0x35ec,
		0x3600, 0x5624,
		0x56cc, 0x575c,
		0x580c, 0x5814,
		0x5890, 0x58bc,
		0x5940, 0x595c,
		0x5980, 0x598c,
		0x59b0, 0x59dc,
		0x59fc, 0x5a18,
		0x5a60, 0x5a6c,
		0x5a80, 0x5a9c,
		0x5b94, 0x5bfc,
		0x5c10, 0x5ec0,
		0x5ec8, 0x5ecc,
		0x6000, 0x6040,
		0x6058, 0x615c,
		0x7700, 0x7798,
		0x77c0, 0x7880,
		0x78cc, 0x78fc,
		0x7b00, 0x7c54,
		0x7d00, 0x7efc,
		0x8dc0, 0x8de0,
		0x8df8, 0x8e84,
		0x8ea0, 0x8f88,
		0x8fb8, 0x911c,
		0x9400, 0x9470,
		0x9600, 0x971c,
		0x9800, 0x9808,
		0x9820, 0x983c,
		0x9850, 0x9864,
		0x9c00, 0x9c6c,
		0x9c80, 0x9cec,
		0x9d00, 0x9d6c,
		0x9d80, 0x9dec,
		0x9e00, 0x9e6c,
		0x9e80, 0x9eec,
		0x9f00, 0x9f6c,
		0x9f80, 0xa020,
		0xd004, 0xd03c,
		0xd100, 0xd118,
		0xd200, 0xd31c,
		0xdfc0, 0xdfe0,
		0xe000, 0xf008,
		0x11000, 0x11014,
		0x11048, 0x11110,
		0x11118, 0x1117c,
		0x11190, 0x11264,
		0x11300, 0x1130c,
		0x12000, 0x1206c,
		0x19040, 0x1906c,
		0x19078, 0x19080,
		0x1908c, 0x19124,
		0x19150, 0x191b0,
		0x191d0, 0x191e8,
		0x19238, 0x192bc,
		0x193f8, 0x19474,
		0x19490, 0x194cc,
		0x194f0, 0x194f8,
		0x19c00, 0x19c80,
		0x19c94, 0x19cbc,
		0x19ce4, 0x19d28,
		0x19d50, 0x19d78,
		0x19d94, 0x19dc8,
		0x19df0, 0x19e10,
		0x19e50, 0x19e6c,
		0x19ea0, 0x19f34,
		0x19f40, 0x19f50,
		0x19f90, 0x19fac,
		0x19fc4, 0x19fe4,
		0x1a000, 0x1a06c,
		0x1a0b0, 0x1a120,
		0x1a128, 0x1a138,
		0x1a190, 0x1a1c4,
		0x1a1fc, 0x1a1fc,
		0x1e008, 0x1e00c,
		0x1e040, 0x1e04c,
		0x1e284, 0x1e290,
		0x1e2c0, 0x1e2c0,
		0x1e2e0, 0x1e2e0,
		0x1e300, 0x1e384,
		0x1e3c0, 0x1e3c8,
		0x1e408, 0x1e40c,
		0x1e440, 0x1e44c,
		0x1e684, 0x1e690,
		0x1e6c0, 0x1e6c0,
		0x1e6e0, 0x1e6e0,
		0x1e700, 0x1e784,
		0x1e7c0, 0x1e7c8,
		0x1e808, 0x1e80c,
		0x1e840, 0x1e84c,
		0x1ea84, 0x1ea90,
		0x1eac0, 0x1eac0,
		0x1eae0, 0x1eae0,
		0x1eb00, 0x1eb84,
		0x1ebc0, 0x1ebc8,
		0x1ec08, 0x1ec0c,
		0x1ec40, 0x1ec4c,
		0x1ee84, 0x1ee90,
		0x1eec0, 0x1eec0,
		0x1eee0, 0x1eee0,
		0x1ef00, 0x1ef84,
		0x1efc0, 0x1efc8,
		0x1f008, 0x1f00c,
		0x1f040, 0x1f04c,
		0x1f284, 0x1f290,
		0x1f2c0, 0x1f2c0,
		0x1f2e0, 0x1f2e0,
		0x1f300, 0x1f384,
		0x1f3c0, 0x1f3c8,
		0x1f408, 0x1f40c,
		0x1f440, 0x1f44c,
		0x1f684, 0x1f690,
		0x1f6c0, 0x1f6c0,
		0x1f6e0, 0x1f6e0,
		0x1f700, 0x1f784,
		0x1f7c0, 0x1f7c8,
		0x1f808, 0x1f80c,
		0x1f840, 0x1f84c,
		0x1fa84, 0x1fa90,
		0x1fac0, 0x1fac0,
		0x1fae0, 0x1fae0,
		0x1fb00, 0x1fb84,
		0x1fbc0, 0x1fbc8,
		0x1fc08, 0x1fc0c,
		0x1fc40, 0x1fc4c,
		0x1fe84, 0x1fe90,
		0x1fec0, 0x1fec0,
		0x1fee0, 0x1fee0,
		0x1ff00, 0x1ff84,
		0x1ffc0, 0x1ffc8,
		0x30000, 0x30070,
		0x30100, 0x3015c,
		0x30190, 0x301d0,
		0x30200, 0x30318,
		0x30400, 0x3052c,
		0x30540, 0x3061c,
		0x30800, 0x30890,
		0x308c0, 0x30908,
		0x30910, 0x309b8,
		0x30a00, 0x30a04,
		0x30a0c, 0x30a2c,
		0x30a44, 0x30a50,
		0x30a74, 0x30c24,
		0x30d00, 0x30d3c,
		0x30d44, 0x30d7c,
		0x30de0, 0x30de0,
		0x30e00, 0x30ed4,
		0x30f00, 0x30fa4,
		0x30fc0, 0x30fc4,
		0x31000, 0x31004,
		0x31080, 0x310fc,
		0x31208, 0x31220,
		0x3123c, 0x31254,
		0x31300, 0x31300,
		0x31308, 0x3131c,
		0x31338, 0x3133c,
		0x31380, 0x31380,
		0x31388, 0x313a8,
		0x313b4, 0x313b4,
		0x31400, 0x31420,
		0x31438, 0x3143c,
		0x31480, 0x31480,
		0x314a8, 0x314a8,
		0x314b0, 0x314b4,
		0x314c8, 0x314d4,
		0x31a40, 0x31a4c,
		0x31af0, 0x31b20,
		0x31b38, 0x31b3c,
		0x31b80, 0x31b80,
		0x31ba8, 0x31ba8,
		0x31bb0, 0x31bb4,
		0x31bc8, 0x31bd4,
		0x32140, 0x3218c,
		0x321f0, 0x32200,
		0x32218, 0x32218,
		0x32400, 0x32400,
		0x32408, 0x3241c,
		0x32618, 0x32620,
		0x32664, 0x32664,
		0x326a8, 0x326a8,
		0x326ec, 0x326ec,
		0x32a00, 0x32abc,
		0x32b00, 0x32b78,
		0x32c00, 0x32c00,
		0x32c08, 0x32c3c,
		0x32e00, 0x32e2c,
		0x32f00, 0x32f2c,
		0x33000, 0x330ac,
		0x330c0, 0x331ac,
		0x331c0, 0x332c4,
		0x332e4, 0x333c4,
		0x333e4, 0x334ac,
		0x334c0, 0x335ac,
		0x335c0, 0x336c4,
		0x336e4, 0x337c4,
		0x337e4, 0x337fc,
		0x33814, 0x33814,
		0x33854, 0x33868,
		0x33880, 0x3388c,
		0x338c0, 0x338d0,
		0x338e8, 0x338ec,
		0x33900, 0x339ac,
		0x339c0, 0x33ac4,
		0x33ae4, 0x33b10,
		0x33b24, 0x33b50,
		0x33bf0, 0x33c10,
		0x33c24, 0x33c50,
		0x33cf0, 0x33cfc,
		0x34000, 0x34070,
		0x34100, 0x3415c,
		0x34190, 0x341d0,
		0x34200, 0x34318,
		0x34400, 0x3452c,
		0x34540, 0x3461c,
		0x34800, 0x34890,
		0x348c0, 0x34908,
		0x34910, 0x349b8,
		0x34a00, 0x34a04,
		0x34a0c, 0x34a2c,
		0x34a44, 0x34a50,
		0x34a74, 0x34c24,
		0x34d00, 0x34d3c,
		0x34d44, 0x34d7c,
		0x34de0, 0x34de0,
		0x34e00, 0x34ed4,
		0x34f00, 0x34fa4,
		0x34fc0, 0x34fc4,
		0x35000, 0x35004,
		0x35080, 0x350fc,
		0x35208, 0x35220,
		0x3523c, 0x35254,
		0x35300, 0x35300,
		0x35308, 0x3531c,
		0x35338, 0x3533c,
		0x35380, 0x35380,
		0x35388, 0x353a8,
		0x353b4, 0x353b4,
		0x35400, 0x35420,
		0x35438, 0x3543c,
		0x35480, 0x35480,
		0x354a8, 0x354a8,
		0x354b0, 0x354b4,
		0x354c8, 0x354d4,
		0x35a40, 0x35a4c,
		0x35af0, 0x35b20,
		0x35b38, 0x35b3c,
		0x35b80, 0x35b80,
		0x35ba8, 0x35ba8,
		0x35bb0, 0x35bb4,
		0x35bc8, 0x35bd4,
		0x36140, 0x3618c,
		0x361f0, 0x36200,
		0x36218, 0x36218,
		0x36400, 0x36400,
		0x36408, 0x3641c,
		0x36618, 0x36620,
		0x36664, 0x36664,
		0x366a8, 0x366a8,
		0x366ec, 0x366ec,
		0x36a00, 0x36abc,
		0x36b00, 0x36b78,
		0x36c00, 0x36c00,
		0x36c08, 0x36c3c,
		0x36e00, 0x36e2c,
		0x36f00, 0x36f2c,
		0x37000, 0x370ac,
		0x370c0, 0x371ac,
		0x371c0, 0x372c4,
		0x372e4, 0x373c4,
		0x373e4, 0x374ac,
		0x374c0, 0x375ac,
		0x375c0, 0x376c4,
		0x376e4, 0x377c4,
		0x377e4, 0x377fc,
		0x37814, 0x37814,
		0x37854, 0x37868,
		0x37880, 0x3788c,
		0x378c0, 0x378d0,
		0x378e8, 0x378ec,
		0x37900, 0x379ac,
		0x379c0, 0x37ac4,
		0x37ae4, 0x37b10,
		0x37b24, 0x37b50,
		0x37bf0, 0x37c10,
		0x37c24, 0x37c50,
		0x37cf0, 0x37cfc,
		0x40040, 0x40040,
		0x40080, 0x40084,
		0x40100, 0x40100,
		0x40140, 0x401bc,
		0x40200, 0x40214,
		0x40228, 0x40228,
		0x40240, 0x40258,
		0x40280, 0x40280,
		0x40304, 0x40304,
		0x40330, 0x4033c,
		0x41304, 0x413dc,
		0x41400, 0x4141c,
		0x41480, 0x414d0,
		0x44000, 0x4407c,
		0x440c0, 0x4427c,
		0x442c0, 0x4447c,
		0x444c0, 0x4467c,
		0x446c0, 0x4487c,
		0x448c0, 0x44a7c,
		0x44ac0, 0x44c7c,
		0x44cc0, 0x44e7c,
		0x44ec0, 0x4507c,
		0x450c0, 0x451fc,
		0x45800, 0x45868,
		0x45880, 0x45884,
		0x458a0, 0x458b0,
		0x45a00, 0x45a68,
		0x45a80, 0x45a84,
		0x45aa0, 0x45ab0,
		0x460c0, 0x460e4,
		0x47000, 0x4708c,
		0x47200, 0x47250,
		0x47400, 0x47420,
		0x47600, 0x47618,
		0x47800, 0x4782c,
		0x50000, 0x500cc,
		0x50400, 0x50400,
		0x50800, 0x508cc,
		0x50c00, 0x50c00,
		0x51000, 0x510b0,
		0x51300, 0x51324,
	};

	u32 *buf_end = (u32 *)((char *)buf + buf_size);
	const unsigned int *reg_ranges;
	int reg_ranges_size, range;
	unsigned int chip_version = CHELSIO_CHIP_VERSION(adap->params.chip);

	/* Select the right set of register ranges to dump depending on the
	 * adapter chip type.
	 */
	switch (chip_version) {
	case CHELSIO_T4:
		reg_ranges = t4_reg_ranges;
		reg_ranges_size = ARRAY_SIZE(t4_reg_ranges);
		break;

	case CHELSIO_T5:
		reg_ranges = t5_reg_ranges;
		reg_ranges_size = ARRAY_SIZE(t5_reg_ranges);
		break;

	case CHELSIO_T6:
		reg_ranges = t6_reg_ranges;
		reg_ranges_size = ARRAY_SIZE(t6_reg_ranges);
		break;

	default:
		dev_err(adap->pdev_dev,
			"Unsupported chip version %d\n", chip_version);
		return;
	}

	/* Clear the register buffer and insert the appropriate register
	 * values selected by the above register ranges.
	 */
	memset(buf, 0, buf_size);
	for (range = 0; range < reg_ranges_size; range += 2) {
		unsigned int reg = reg_ranges[range];
		unsigned int last_reg = reg_ranges[range + 1];
		u32 *bufp = (u32 *)((char *)buf + reg);

		/* Iterate across the register range filling in the register
		 * buffer but don't write past the end of the register buffer.
		 */
		while (reg <= last_reg && bufp < buf_end) {
			*bufp++ = t4_read_reg(adap, reg);
			reg += sizeof(u32);
		}
	}
}

#define EEPROM_STAT_ADDR   0x7bfc
#define VPD_BASE           0x400
#define VPD_BASE_OLD       0
#define VPD_LEN            1024
#define CHELSIO_VPD_UNIQUE_ID 0x82

/**
 *	t4_seeprom_wp - enable/disable EEPROM write protection
 *	@adapter: the adapter
 *	@enable: whether to enable or disable write protection
 *
 *	Enables or disables write protection on the serial EEPROM.
 */
int t4_seeprom_wp(struct adapter *adapter, bool enable)
{
	unsigned int v = enable ? 0xc : 0;
	int ret = pci_write_vpd(adapter->pdev, EEPROM_STAT_ADDR, 4, &v);
	return ret < 0 ? ret : 0;
}

/**
 *	t4_get_raw_vpd_params - read VPD parameters from VPD EEPROM
 *	@adapter: adapter to read
 *	@p: where to store the parameters
 *
 *	Reads card parameters stored in VPD EEPROM.
 */
int t4_get_raw_vpd_params(struct adapter *adapter, struct vpd_params *p)
{
	int i, ret = 0, addr;
	int ec, sn, pn, na;
	u8 *vpd, csum;
	unsigned int vpdr_len, kw_offset, id_len;

	vpd = vmalloc(VPD_LEN);
	if (!vpd)
		return -ENOMEM;

	/* Card information normally starts at VPD_BASE but early cards had
	 * it at 0.
	 */
	ret = pci_read_vpd(adapter->pdev, VPD_BASE, sizeof(u32), vpd);
	if (ret < 0)
		goto out;

	/* The VPD shall have a unique identifier specified by the PCI SIG.
	 * For chelsio adapters, the identifier is 0x82. The first byte of a VPD
	 * shall be CHELSIO_VPD_UNIQUE_ID (0x82). The VPD programming software
	 * is expected to automatically put this entry at the
	 * beginning of the VPD.
	 */
	addr = *vpd == CHELSIO_VPD_UNIQUE_ID ? VPD_BASE : VPD_BASE_OLD;

	ret = pci_read_vpd(adapter->pdev, addr, VPD_LEN, vpd);
	if (ret < 0)
		goto out;

	if (vpd[0] != PCI_VPD_LRDT_ID_STRING) {
		dev_err(adapter->pdev_dev, "missing VPD ID string\n");
		ret = -EINVAL;
		goto out;
	}

	id_len = pci_vpd_lrdt_size(vpd);
	if (id_len > ID_LEN)
		id_len = ID_LEN;

	i = pci_vpd_find_tag(vpd, 0, VPD_LEN, PCI_VPD_LRDT_RO_DATA);
	if (i < 0) {
		dev_err(adapter->pdev_dev, "missing VPD-R section\n");
		ret = -EINVAL;
		goto out;
	}

	vpdr_len = pci_vpd_lrdt_size(&vpd[i]);
	kw_offset = i + PCI_VPD_LRDT_TAG_SIZE;
	if (vpdr_len + kw_offset > VPD_LEN) {
		dev_err(adapter->pdev_dev, "bad VPD-R length %u\n", vpdr_len);
		ret = -EINVAL;
		goto out;
	}

#define FIND_VPD_KW(var, name) do { \
	var = pci_vpd_find_info_keyword(vpd, kw_offset, vpdr_len, name); \
	if (var < 0) { \
		dev_err(adapter->pdev_dev, "missing VPD keyword " name "\n"); \
		ret = -EINVAL; \
		goto out; \
	} \
	var += PCI_VPD_INFO_FLD_HDR_SIZE; \
} while (0)

	FIND_VPD_KW(i, "RV");
	for (csum = 0; i >= 0; i--)
		csum += vpd[i];

	if (csum) {
		dev_err(adapter->pdev_dev,
			"corrupted VPD EEPROM, actual csum %u\n", csum);
		ret = -EINVAL;
		goto out;
	}

	FIND_VPD_KW(ec, "EC");
	FIND_VPD_KW(sn, "SN");
	FIND_VPD_KW(pn, "PN");
	FIND_VPD_KW(na, "NA");
#undef FIND_VPD_KW

	memcpy(p->id, vpd + PCI_VPD_LRDT_TAG_SIZE, id_len);
	strim(p->id);
	memcpy(p->ec, vpd + ec, EC_LEN);
	strim(p->ec);
	i = pci_vpd_info_field_size(vpd + sn - PCI_VPD_INFO_FLD_HDR_SIZE);
	memcpy(p->sn, vpd + sn, min(i, SERNUM_LEN));
	strim(p->sn);
	i = pci_vpd_info_field_size(vpd + pn - PCI_VPD_INFO_FLD_HDR_SIZE);
	memcpy(p->pn, vpd + pn, min(i, PN_LEN));
	strim(p->pn);
	memcpy(p->na, vpd + na, min(i, MACADDR_LEN));
	strim((char *)p->na);

out:
	vfree(vpd);
	return ret;
}

/**
 *	t4_get_vpd_params - read VPD parameters & retrieve Core Clock
 *	@adapter: adapter to read
 *	@p: where to store the parameters
 *
 *	Reads card parameters stored in VPD EEPROM and retrieves the Core
 *	Clock.  This can only be called after a connection to the firmware
 *	is established.
 */
int t4_get_vpd_params(struct adapter *adapter, struct vpd_params *p)
{
	u32 cclk_param, cclk_val;
	int ret;

	/* Grab the raw VPD parameters.
	 */
	ret = t4_get_raw_vpd_params(adapter, p);
	if (ret)
		return ret;

	/* Ask firmware for the Core Clock since it knows how to translate the
	 * Reference Clock ('V2') VPD field into a Core Clock value ...
	 */
	cclk_param = (FW_PARAMS_MNEM_V(FW_PARAMS_MNEM_DEV) |
		      FW_PARAMS_PARAM_X_V(FW_PARAMS_PARAM_DEV_CCLK));
	ret = t4_query_params(adapter, adapter->mbox, adapter->pf, 0,
			      1, &cclk_param, &cclk_val);

	if (ret)
		return ret;
	p->cclk = cclk_val;

	return 0;
}

/* serial flash and firmware constants */
enum {
	SF_ATTEMPTS = 10,             /* max retries for SF operations */

	/* flash command opcodes */
	SF_PROG_PAGE    = 2,          /* program page */
	SF_WR_DISABLE   = 4,          /* disable writes */
	SF_RD_STATUS    = 5,          /* read status register */
	SF_WR_ENABLE    = 6,          /* enable writes */
	SF_RD_DATA_FAST = 0xb,        /* read flash */
	SF_RD_ID        = 0x9f,       /* read ID */
	SF_ERASE_SECTOR = 0xd8,       /* erase sector */

	FW_MAX_SIZE = 16 * SF_SEC_SIZE,
};

/**
 *	sf1_read - read data from the serial flash
 *	@adapter: the adapter
 *	@byte_cnt: number of bytes to read
 *	@cont: whether another operation will be chained
 *	@lock: whether to lock SF for PL access only
 *	@valp: where to store the read data
 *
 *	Reads up to 4 bytes of data from the serial flash.  The location of
 *	the read needs to be specified prior to calling this by issuing the
 *	appropriate commands to the serial flash.
 */
static int sf1_read(struct adapter *adapter, unsigned int byte_cnt, int cont,
		    int lock, u32 *valp)
{
	int ret;

	if (!byte_cnt || byte_cnt > 4)
		return -EINVAL;
	if (t4_read_reg(adapter, SF_OP_A) & SF_BUSY_F)
		return -EBUSY;
	t4_write_reg(adapter, SF_OP_A, SF_LOCK_V(lock) |
		     SF_CONT_V(cont) | BYTECNT_V(byte_cnt - 1));
	ret = t4_wait_op_done(adapter, SF_OP_A, SF_BUSY_F, 0, SF_ATTEMPTS, 5);
	if (!ret)
		*valp = t4_read_reg(adapter, SF_DATA_A);
	return ret;
}

/**
 *	sf1_write - write data to the serial flash
 *	@adapter: the adapter
 *	@byte_cnt: number of bytes to write
 *	@cont: whether another operation will be chained
 *	@lock: whether to lock SF for PL access only
 *	@val: value to write
 *
 *	Writes up to 4 bytes of data to the serial flash.  The location of
 *	the write needs to be specified prior to calling this by issuing the
 *	appropriate commands to the serial flash.
 */
static int sf1_write(struct adapter *adapter, unsigned int byte_cnt, int cont,
		     int lock, u32 val)
{
	if (!byte_cnt || byte_cnt > 4)
		return -EINVAL;
	if (t4_read_reg(adapter, SF_OP_A) & SF_BUSY_F)
		return -EBUSY;
	t4_write_reg(adapter, SF_DATA_A, val);
	t4_write_reg(adapter, SF_OP_A, SF_LOCK_V(lock) |
		     SF_CONT_V(cont) | BYTECNT_V(byte_cnt - 1) | OP_V(1));
	return t4_wait_op_done(adapter, SF_OP_A, SF_BUSY_F, 0, SF_ATTEMPTS, 5);
}

/**
 *	flash_wait_op - wait for a flash operation to complete
 *	@adapter: the adapter
 *	@attempts: max number of polls of the status register
 *	@delay: delay between polls in ms
 *
 *	Wait for a flash operation to complete by polling the status register.
 */
static int flash_wait_op(struct adapter *adapter, int attempts, int delay)
{
	int ret;
	u32 status;

	while (1) {
		if ((ret = sf1_write(adapter, 1, 1, 1, SF_RD_STATUS)) != 0 ||
		    (ret = sf1_read(adapter, 1, 0, 1, &status)) != 0)
			return ret;
		if (!(status & 1))
			return 0;
		if (--attempts == 0)
			return -EAGAIN;
		if (delay)
			msleep(delay);
	}
}

/**
 *	t4_read_flash - read words from serial flash
 *	@adapter: the adapter
 *	@addr: the start address for the read
 *	@nwords: how many 32-bit words to read
 *	@data: where to store the read data
 *	@byte_oriented: whether to store data as bytes or as words
 *
 *	Read the specified number of 32-bit words from the serial flash.
 *	If @byte_oriented is set the read data is stored as a byte array
 *	(i.e., big-endian), otherwise as 32-bit words in the platform's
 *	natural endianness.
 */
int t4_read_flash(struct adapter *adapter, unsigned int addr,
		  unsigned int nwords, u32 *data, int byte_oriented)
{
	int ret;

	if (addr + nwords * sizeof(u32) > adapter->params.sf_size || (addr & 3))
		return -EINVAL;

	addr = swab32(addr) | SF_RD_DATA_FAST;

	if ((ret = sf1_write(adapter, 4, 1, 0, addr)) != 0 ||
	    (ret = sf1_read(adapter, 1, 1, 0, data)) != 0)
		return ret;

	for ( ; nwords; nwords--, data++) {
		ret = sf1_read(adapter, 4, nwords > 1, nwords == 1, data);
		if (nwords == 1)
			t4_write_reg(adapter, SF_OP_A, 0);    /* unlock SF */
		if (ret)
			return ret;
		if (byte_oriented)
			*data = (__force __u32)(cpu_to_be32(*data));
	}
	return 0;
}

/**
 *	t4_write_flash - write up to a page of data to the serial flash
 *	@adapter: the adapter
 *	@addr: the start address to write
 *	@n: length of data to write in bytes
 *	@data: the data to write
 *
 *	Writes up to a page of data (256 bytes) to the serial flash starting
 *	at the given address.  All the data must be written to the same page.
 */
static int t4_write_flash(struct adapter *adapter, unsigned int addr,
			  unsigned int n, const u8 *data)
{
	int ret;
	u32 buf[64];
	unsigned int i, c, left, val, offset = addr & 0xff;

	if (addr >= adapter->params.sf_size || offset + n > SF_PAGE_SIZE)
		return -EINVAL;

	val = swab32(addr) | SF_PROG_PAGE;

	if ((ret = sf1_write(adapter, 1, 0, 1, SF_WR_ENABLE)) != 0 ||
	    (ret = sf1_write(adapter, 4, 1, 1, val)) != 0)
		goto unlock;

	for (left = n; left; left -= c) {
		c = min(left, 4U);
		for (val = 0, i = 0; i < c; ++i)
			val = (val << 8) + *data++;

		ret = sf1_write(adapter, c, c != left, 1, val);
		if (ret)
			goto unlock;
	}
	ret = flash_wait_op(adapter, 8, 1);
	if (ret)
		goto unlock;

	t4_write_reg(adapter, SF_OP_A, 0);    /* unlock SF */

	/* Read the page to verify the write succeeded */
	ret = t4_read_flash(adapter, addr & ~0xff, ARRAY_SIZE(buf), buf, 1);
	if (ret)
		return ret;

	if (memcmp(data - n, (u8 *)buf + offset, n)) {
		dev_err(adapter->pdev_dev,
			"failed to correctly write the flash page at %#x\n",
			addr);
		return -EIO;
	}
	return 0;

unlock:
	t4_write_reg(adapter, SF_OP_A, 0);    /* unlock SF */
	return ret;
}

/**
 *	t4_get_fw_version - read the firmware version
 *	@adapter: the adapter
 *	@vers: where to place the version
 *
 *	Reads the FW version from flash.
 */
int t4_get_fw_version(struct adapter *adapter, u32 *vers)
{
	return t4_read_flash(adapter, FLASH_FW_START +
			     offsetof(struct fw_hdr, fw_ver), 1,
			     vers, 0);
}

/**
 *	t4_get_tp_version - read the TP microcode version
 *	@adapter: the adapter
 *	@vers: where to place the version
 *
 *	Reads the TP microcode version from flash.
 */
int t4_get_tp_version(struct adapter *adapter, u32 *vers)
{
	return t4_read_flash(adapter, FLASH_FW_START +
			     offsetof(struct fw_hdr, tp_microcode_ver),
			     1, vers, 0);
}

/**
 *	t4_get_exprom_version - return the Expansion ROM version (if any)
 *	@adapter: the adapter
 *	@vers: where to place the version
 *
 *	Reads the Expansion ROM header from FLASH and returns the version
 *	number (if present) through the @vers return value pointer.  We return
 *	this in the Firmware Version Format since it's convenient.  Return
 *	0 on success, -ENOENT if no Expansion ROM is present.
 */
int t4_get_exprom_version(struct adapter *adap, u32 *vers)
{
	struct exprom_header {
		unsigned char hdr_arr[16];	/* must start with 0x55aa */
		unsigned char hdr_ver[4];	/* Expansion ROM version */
	} *hdr;
	u32 exprom_header_buf[DIV_ROUND_UP(sizeof(struct exprom_header),
					   sizeof(u32))];
	int ret;

	ret = t4_read_flash(adap, FLASH_EXP_ROM_START,
			    ARRAY_SIZE(exprom_header_buf), exprom_header_buf,
			    0);
	if (ret)
		return ret;

	hdr = (struct exprom_header *)exprom_header_buf;
	if (hdr->hdr_arr[0] != 0x55 || hdr->hdr_arr[1] != 0xaa)
		return -ENOENT;

	*vers = (FW_HDR_FW_VER_MAJOR_V(hdr->hdr_ver[0]) |
		 FW_HDR_FW_VER_MINOR_V(hdr->hdr_ver[1]) |
		 FW_HDR_FW_VER_MICRO_V(hdr->hdr_ver[2]) |
		 FW_HDR_FW_VER_BUILD_V(hdr->hdr_ver[3]));
	return 0;
}

/* Is the given firmware API compatible with the one the driver was compiled
 * with?
 */
static int fw_compatible(const struct fw_hdr *hdr1, const struct fw_hdr *hdr2)
{

	/* short circuit if it's the exact same firmware version */
	if (hdr1->chip == hdr2->chip && hdr1->fw_ver == hdr2->fw_ver)
		return 1;

#define SAME_INTF(x) (hdr1->intfver_##x == hdr2->intfver_##x)
	if (hdr1->chip == hdr2->chip && SAME_INTF(nic) && SAME_INTF(vnic) &&
	    SAME_INTF(ri) && SAME_INTF(iscsi) && SAME_INTF(fcoe))
		return 1;
#undef SAME_INTF

	return 0;
}

/* The firmware in the filesystem is usable, but should it be installed?
 * This routine explains itself in detail if it indicates the filesystem
 * firmware should be installed.
 */
static int should_install_fs_fw(struct adapter *adap, int card_fw_usable,
				int k, int c)
{
	const char *reason;

	if (!card_fw_usable) {
		reason = "incompatible or unusable";
		goto install;
	}

	if (k > c) {
		reason = "older than the version supported with this driver";
		goto install;
	}

	return 0;

install:
	dev_err(adap->pdev_dev, "firmware on card (%u.%u.%u.%u) is %s, "
		"installing firmware %u.%u.%u.%u on card.\n",
		FW_HDR_FW_VER_MAJOR_G(c), FW_HDR_FW_VER_MINOR_G(c),
		FW_HDR_FW_VER_MICRO_G(c), FW_HDR_FW_VER_BUILD_G(c), reason,
		FW_HDR_FW_VER_MAJOR_G(k), FW_HDR_FW_VER_MINOR_G(k),
		FW_HDR_FW_VER_MICRO_G(k), FW_HDR_FW_VER_BUILD_G(k));

	return 1;
}

int t4_prep_fw(struct adapter *adap, struct fw_info *fw_info,
	       const u8 *fw_data, unsigned int fw_size,
	       struct fw_hdr *card_fw, enum dev_state state,
	       int *reset)
{
	int ret, card_fw_usable, fs_fw_usable;
	const struct fw_hdr *fs_fw;
	const struct fw_hdr *drv_fw;

	drv_fw = &fw_info->fw_hdr;

	/* Read the header of the firmware on the card */
	ret = -t4_read_flash(adap, FLASH_FW_START,
			    sizeof(*card_fw) / sizeof(uint32_t),
			    (uint32_t *)card_fw, 1);
	if (ret == 0) {
		card_fw_usable = fw_compatible(drv_fw, (const void *)card_fw);
	} else {
		dev_err(adap->pdev_dev,
			"Unable to read card's firmware header: %d\n", ret);
		card_fw_usable = 0;
	}

	if (fw_data != NULL) {
		fs_fw = (const void *)fw_data;
		fs_fw_usable = fw_compatible(drv_fw, fs_fw);
	} else {
		fs_fw = NULL;
		fs_fw_usable = 0;
	}

	if (card_fw_usable && card_fw->fw_ver == drv_fw->fw_ver &&
	    (!fs_fw_usable || fs_fw->fw_ver == drv_fw->fw_ver)) {
		/* Common case: the firmware on the card is an exact match and
		 * the filesystem one is an exact match too, or the filesystem
		 * one is absent/incompatible.
		 */
	} else if (fs_fw_usable && state == DEV_STATE_UNINIT &&
		   should_install_fs_fw(adap, card_fw_usable,
					be32_to_cpu(fs_fw->fw_ver),
					be32_to_cpu(card_fw->fw_ver))) {
		ret = -t4_fw_upgrade(adap, adap->mbox, fw_data,
				     fw_size, 0);
		if (ret != 0) {
			dev_err(adap->pdev_dev,
				"failed to install firmware: %d\n", ret);
			goto bye;
		}

		/* Installed successfully, update the cached header too. */
		*card_fw = *fs_fw;
		card_fw_usable = 1;
		*reset = 0;	/* already reset as part of load_fw */
	}

	if (!card_fw_usable) {
		uint32_t d, c, k;

		d = be32_to_cpu(drv_fw->fw_ver);
		c = be32_to_cpu(card_fw->fw_ver);
		k = fs_fw ? be32_to_cpu(fs_fw->fw_ver) : 0;

		dev_err(adap->pdev_dev, "Cannot find a usable firmware: "
			"chip state %d, "
			"driver compiled with %d.%d.%d.%d, "
			"card has %d.%d.%d.%d, filesystem has %d.%d.%d.%d\n",
			state,
			FW_HDR_FW_VER_MAJOR_G(d), FW_HDR_FW_VER_MINOR_G(d),
			FW_HDR_FW_VER_MICRO_G(d), FW_HDR_FW_VER_BUILD_G(d),
			FW_HDR_FW_VER_MAJOR_G(c), FW_HDR_FW_VER_MINOR_G(c),
			FW_HDR_FW_VER_MICRO_G(c), FW_HDR_FW_VER_BUILD_G(c),
			FW_HDR_FW_VER_MAJOR_G(k), FW_HDR_FW_VER_MINOR_G(k),
			FW_HDR_FW_VER_MICRO_G(k), FW_HDR_FW_VER_BUILD_G(k));
		ret = EINVAL;
		goto bye;
	}

	/* We're using whatever's on the card and it's known to be good. */
	adap->params.fw_vers = be32_to_cpu(card_fw->fw_ver);
	adap->params.tp_vers = be32_to_cpu(card_fw->tp_microcode_ver);

bye:
	return ret;
}

/**
 *	t4_flash_erase_sectors - erase a range of flash sectors
 *	@adapter: the adapter
 *	@start: the first sector to erase
 *	@end: the last sector to erase
 *
 *	Erases the sectors in the given inclusive range.
 */
static int t4_flash_erase_sectors(struct adapter *adapter, int start, int end)
{
	int ret = 0;

	if (end >= adapter->params.sf_nsec)
		return -EINVAL;

	while (start <= end) {
		if ((ret = sf1_write(adapter, 1, 0, 1, SF_WR_ENABLE)) != 0 ||
		    (ret = sf1_write(adapter, 4, 0, 1,
				     SF_ERASE_SECTOR | (start << 8))) != 0 ||
		    (ret = flash_wait_op(adapter, 14, 500)) != 0) {
			dev_err(adapter->pdev_dev,
				"erase of flash sector %d failed, error %d\n",
				start, ret);
			break;
		}
		start++;
	}
	t4_write_reg(adapter, SF_OP_A, 0);    /* unlock SF */
	return ret;
}

/**
 *	t4_flash_cfg_addr - return the address of the flash configuration file
 *	@adapter: the adapter
 *
 *	Return the address within the flash where the Firmware Configuration
 *	File is stored.
 */
unsigned int t4_flash_cfg_addr(struct adapter *adapter)
{
	if (adapter->params.sf_size == 0x100000)
		return FLASH_FPGA_CFG_START;
	else
		return FLASH_CFG_START;
}

/* Return TRUE if the specified firmware matches the adapter.  I.e. T4
 * firmware for T4 adapters, T5 firmware for T5 adapters, etc.  We go ahead
 * and emit an error message for mismatched firmware to save our caller the
 * effort ...
 */
static bool t4_fw_matches_chip(const struct adapter *adap,
			       const struct fw_hdr *hdr)
{
	/* The expression below will return FALSE for any unsupported adapter
	 * which will keep us "honest" in the future ...
	 */
	if ((is_t4(adap->params.chip) && hdr->chip == FW_HDR_CHIP_T4) ||
	    (is_t5(adap->params.chip) && hdr->chip == FW_HDR_CHIP_T5) ||
	    (is_t6(adap->params.chip) && hdr->chip == FW_HDR_CHIP_T6))
		return true;

	dev_err(adap->pdev_dev,
		"FW image (%d) is not suitable for this adapter (%d)\n",
		hdr->chip, CHELSIO_CHIP_VERSION(adap->params.chip));
	return false;
}

/**
 *	t4_load_fw - download firmware
 *	@adap: the adapter
 *	@fw_data: the firmware image to write
 *	@size: image size
 *
 *	Write the supplied firmware image to the card's serial flash.
 */
int t4_load_fw(struct adapter *adap, const u8 *fw_data, unsigned int size)
{
	u32 csum;
	int ret, addr;
	unsigned int i;
	u8 first_page[SF_PAGE_SIZE];
	const __be32 *p = (const __be32 *)fw_data;
	const struct fw_hdr *hdr = (const struct fw_hdr *)fw_data;
	unsigned int sf_sec_size = adap->params.sf_size / adap->params.sf_nsec;
	unsigned int fw_img_start = adap->params.sf_fw_start;
	unsigned int fw_start_sec = fw_img_start / sf_sec_size;

	if (!size) {
		dev_err(adap->pdev_dev, "FW image has no data\n");
		return -EINVAL;
	}
	if (size & 511) {
		dev_err(adap->pdev_dev,
			"FW image size not multiple of 512 bytes\n");
		return -EINVAL;
	}
	if ((unsigned int)be16_to_cpu(hdr->len512) * 512 != size) {
		dev_err(adap->pdev_dev,
			"FW image size differs from size in FW header\n");
		return -EINVAL;
	}
	if (size > FW_MAX_SIZE) {
		dev_err(adap->pdev_dev, "FW image too large, max is %u bytes\n",
			FW_MAX_SIZE);
		return -EFBIG;
	}
	if (!t4_fw_matches_chip(adap, hdr))
		return -EINVAL;

	for (csum = 0, i = 0; i < size / sizeof(csum); i++)
		csum += be32_to_cpu(p[i]);

	if (csum != 0xffffffff) {
		dev_err(adap->pdev_dev,
			"corrupted firmware image, checksum %#x\n", csum);
		return -EINVAL;
	}

	i = DIV_ROUND_UP(size, sf_sec_size);        /* # of sectors spanned */
	ret = t4_flash_erase_sectors(adap, fw_start_sec, fw_start_sec + i - 1);
	if (ret)
		goto out;

	/*
	 * We write the correct version at the end so the driver can see a bad
	 * version if the FW write fails.  Start by writing a copy of the
	 * first page with a bad version.
	 */
	memcpy(first_page, fw_data, SF_PAGE_SIZE);
	((struct fw_hdr *)first_page)->fw_ver = cpu_to_be32(0xffffffff);
	ret = t4_write_flash(adap, fw_img_start, SF_PAGE_SIZE, first_page);
	if (ret)
		goto out;

	addr = fw_img_start;
	for (size -= SF_PAGE_SIZE; size; size -= SF_PAGE_SIZE) {
		addr += SF_PAGE_SIZE;
		fw_data += SF_PAGE_SIZE;
		ret = t4_write_flash(adap, addr, SF_PAGE_SIZE, fw_data);
		if (ret)
			goto out;
	}

	ret = t4_write_flash(adap,
			     fw_img_start + offsetof(struct fw_hdr, fw_ver),
			     sizeof(hdr->fw_ver), (const u8 *)&hdr->fw_ver);
out:
	if (ret)
		dev_err(adap->pdev_dev, "firmware download failed, error %d\n",
			ret);
	else
		ret = t4_get_fw_version(adap, &adap->params.fw_vers);
	return ret;
}

/**
 *	t4_phy_fw_ver - return current PHY firmware version
 *	@adap: the adapter
 *	@phy_fw_ver: return value buffer for PHY firmware version
 *
 *	Returns the current version of external PHY firmware on the
 *	adapter.
 */
int t4_phy_fw_ver(struct adapter *adap, int *phy_fw_ver)
{
	u32 param, val;
	int ret;

	param = (FW_PARAMS_MNEM_V(FW_PARAMS_MNEM_DEV) |
		 FW_PARAMS_PARAM_X_V(FW_PARAMS_PARAM_DEV_PHYFW) |
		 FW_PARAMS_PARAM_Y_V(adap->params.portvec) |
		 FW_PARAMS_PARAM_Z_V(FW_PARAMS_PARAM_DEV_PHYFW_VERSION));
	ret = t4_query_params(adap, adap->mbox, adap->pf, 0, 1,
			      &param, &val);
	if (ret < 0)
		return ret;
	*phy_fw_ver = val;
	return 0;
}

/**
 *	t4_load_phy_fw - download port PHY firmware
 *	@adap: the adapter
 *	@win: the PCI-E Memory Window index to use for t4_memory_rw()
 *	@win_lock: the lock to use to guard the memory copy
 *	@phy_fw_version: function to check PHY firmware versions
 *	@phy_fw_data: the PHY firmware image to write
 *	@phy_fw_size: image size
 *
 *	Transfer the specified PHY firmware to the adapter.  If a non-NULL
 *	@phy_fw_version is supplied, then it will be used to determine if
 *	it's necessary to perform the transfer by comparing the version
 *	of any existing adapter PHY firmware with that of the passed in
 *	PHY firmware image.  If @win_lock is non-NULL then it will be used
 *	around the call to t4_memory_rw() which transfers the PHY firmware
 *	to the adapter.
 *
 *	A negative error number will be returned if an error occurs.  If
 *	version number support is available and there's no need to upgrade
 *	the firmware, 0 will be returned.  If firmware is successfully
 *	transferred to the adapter, 1 will be retured.
 *
 *	NOTE: some adapters only have local RAM to store the PHY firmware.  As
 *	a result, a RESET of the adapter would cause that RAM to lose its
 *	contents.  Thus, loading PHY firmware on such adapters must happen
 *	after any FW_RESET_CMDs ...
 */
int t4_load_phy_fw(struct adapter *adap,
		   int win, spinlock_t *win_lock,
		   int (*phy_fw_version)(const u8 *, size_t),
		   const u8 *phy_fw_data, size_t phy_fw_size)
{
	unsigned long mtype = 0, maddr = 0;
	u32 param, val;
	int cur_phy_fw_ver = 0, new_phy_fw_vers = 0;
	int ret;

	/* If we have version number support, then check to see if the adapter
	 * already has up-to-date PHY firmware loaded.
	 */
	 if (phy_fw_version) {
		new_phy_fw_vers = phy_fw_version(phy_fw_data, phy_fw_size);
		ret = t4_phy_fw_ver(adap, &cur_phy_fw_ver);
		if (ret < 0)
			return ret;

		if (cur_phy_fw_ver >= new_phy_fw_vers) {
			CH_WARN(adap, "PHY Firmware already up-to-date, "
				"version %#x\n", cur_phy_fw_ver);
			return 0;
		}
	}

	/* Ask the firmware where it wants us to copy the PHY firmware image.
	 * The size of the file requires a special version of the READ coommand
	 * which will pass the file size via the values field in PARAMS_CMD and
	 * retrieve the return value from firmware and place it in the same
	 * buffer values
	 */
	param = (FW_PARAMS_MNEM_V(FW_PARAMS_MNEM_DEV) |
		 FW_PARAMS_PARAM_X_V(FW_PARAMS_PARAM_DEV_PHYFW) |
		 FW_PARAMS_PARAM_Y_V(adap->params.portvec) |
		 FW_PARAMS_PARAM_Z_V(FW_PARAMS_PARAM_DEV_PHYFW_DOWNLOAD));
	val = phy_fw_size;
	ret = t4_query_params_rw(adap, adap->mbox, adap->pf, 0, 1,
				 &param, &val, 1);
	if (ret < 0)
		return ret;
	mtype = val >> 8;
	maddr = (val & 0xff) << 16;

	/* Copy the supplied PHY Firmware image to the adapter memory location
	 * allocated by the adapter firmware.
	 */
	if (win_lock)
		spin_lock_bh(win_lock);
	ret = t4_memory_rw(adap, win, mtype, maddr,
			   phy_fw_size, (__be32 *)phy_fw_data,
			   T4_MEMORY_WRITE);
	if (win_lock)
		spin_unlock_bh(win_lock);
	if (ret)
		return ret;

	/* Tell the firmware that the PHY firmware image has been written to
	 * RAM and it can now start copying it over to the PHYs.  The chip
	 * firmware will RESET the affected PHYs as part of this operation
	 * leaving them running the new PHY firmware image.
	 */
	param = (FW_PARAMS_MNEM_V(FW_PARAMS_MNEM_DEV) |
		 FW_PARAMS_PARAM_X_V(FW_PARAMS_PARAM_DEV_PHYFW) |
		 FW_PARAMS_PARAM_Y_V(adap->params.portvec) |
		 FW_PARAMS_PARAM_Z_V(FW_PARAMS_PARAM_DEV_PHYFW_DOWNLOAD));
	ret = t4_set_params_timeout(adap, adap->mbox, adap->pf, 0, 1,
				    &param, &val, 30000);

	/* If we have version number support, then check to see that the new
	 * firmware got loaded properly.
	 */
	if (phy_fw_version) {
		ret = t4_phy_fw_ver(adap, &cur_phy_fw_ver);
		if (ret < 0)
			return ret;

		if (cur_phy_fw_ver != new_phy_fw_vers) {
			CH_WARN(adap, "PHY Firmware did not update: "
				"version on adapter %#x, "
				"version flashed %#x\n",
				cur_phy_fw_ver, new_phy_fw_vers);
			return -ENXIO;
		}
	}

	return 1;
}

/**
 *	t4_fwcache - firmware cache operation
 *	@adap: the adapter
 *	@op  : the operation (flush or flush and invalidate)
 */
int t4_fwcache(struct adapter *adap, enum fw_params_param_dev_fwcache op)
{
	struct fw_params_cmd c;

	memset(&c, 0, sizeof(c));
	c.op_to_vfn =
		cpu_to_be32(FW_CMD_OP_V(FW_PARAMS_CMD) |
			    FW_CMD_REQUEST_F | FW_CMD_WRITE_F |
			    FW_PARAMS_CMD_PFN_V(adap->pf) |
			    FW_PARAMS_CMD_VFN_V(0));
	c.retval_len16 = cpu_to_be32(FW_LEN16(c));
	c.param[0].mnem =
		cpu_to_be32(FW_PARAMS_MNEM_V(FW_PARAMS_MNEM_DEV) |
			    FW_PARAMS_PARAM_X_V(FW_PARAMS_PARAM_DEV_FWCACHE));
	c.param[0].val = (__force __be32)op;

	return t4_wr_mbox(adap, adap->mbox, &c, sizeof(c), NULL);
}

void t4_cim_read_pif_la(struct adapter *adap, u32 *pif_req, u32 *pif_rsp,
			unsigned int *pif_req_wrptr,
			unsigned int *pif_rsp_wrptr)
{
	int i, j;
	u32 cfg, val, req, rsp;

	cfg = t4_read_reg(adap, CIM_DEBUGCFG_A);
	if (cfg & LADBGEN_F)
		t4_write_reg(adap, CIM_DEBUGCFG_A, cfg ^ LADBGEN_F);

	val = t4_read_reg(adap, CIM_DEBUGSTS_A);
	req = POLADBGWRPTR_G(val);
	rsp = PILADBGWRPTR_G(val);
	if (pif_req_wrptr)
		*pif_req_wrptr = req;
	if (pif_rsp_wrptr)
		*pif_rsp_wrptr = rsp;

	for (i = 0; i < CIM_PIFLA_SIZE; i++) {
		for (j = 0; j < 6; j++) {
			t4_write_reg(adap, CIM_DEBUGCFG_A, POLADBGRDPTR_V(req) |
				     PILADBGRDPTR_V(rsp));
			*pif_req++ = t4_read_reg(adap, CIM_PO_LA_DEBUGDATA_A);
			*pif_rsp++ = t4_read_reg(adap, CIM_PI_LA_DEBUGDATA_A);
			req++;
			rsp++;
		}
		req = (req + 2) & POLADBGRDPTR_M;
		rsp = (rsp + 2) & PILADBGRDPTR_M;
	}
	t4_write_reg(adap, CIM_DEBUGCFG_A, cfg);
}

void t4_cim_read_ma_la(struct adapter *adap, u32 *ma_req, u32 *ma_rsp)
{
	u32 cfg;
	int i, j, idx;

	cfg = t4_read_reg(adap, CIM_DEBUGCFG_A);
	if (cfg & LADBGEN_F)
		t4_write_reg(adap, CIM_DEBUGCFG_A, cfg ^ LADBGEN_F);

	for (i = 0; i < CIM_MALA_SIZE; i++) {
		for (j = 0; j < 5; j++) {
			idx = 8 * i + j;
			t4_write_reg(adap, CIM_DEBUGCFG_A, POLADBGRDPTR_V(idx) |
				     PILADBGRDPTR_V(idx));
			*ma_req++ = t4_read_reg(adap, CIM_PO_LA_MADEBUGDATA_A);
			*ma_rsp++ = t4_read_reg(adap, CIM_PI_LA_MADEBUGDATA_A);
		}
	}
	t4_write_reg(adap, CIM_DEBUGCFG_A, cfg);
}

void t4_ulprx_read_la(struct adapter *adap, u32 *la_buf)
{
	unsigned int i, j;

	for (i = 0; i < 8; i++) {
		u32 *p = la_buf + i;

		t4_write_reg(adap, ULP_RX_LA_CTL_A, i);
		j = t4_read_reg(adap, ULP_RX_LA_WRPTR_A);
		t4_write_reg(adap, ULP_RX_LA_RDPTR_A, j);
		for (j = 0; j < ULPRX_LA_SIZE; j++, p += 8)
			*p = t4_read_reg(adap, ULP_RX_LA_RDDATA_A);
	}
}

#define ADVERT_MASK (FW_PORT_CAP_SPEED_100M | FW_PORT_CAP_SPEED_1G |\
		     FW_PORT_CAP_SPEED_10G | FW_PORT_CAP_SPEED_40G | \
		     FW_PORT_CAP_ANEG)

/**
 *	t4_link_l1cfg - apply link configuration to MAC/PHY
 *	@phy: the PHY to setup
 *	@mac: the MAC to setup
 *	@lc: the requested link configuration
 *
 *	Set up a port's MAC and PHY according to a desired link configuration.
 *	- If the PHY can auto-negotiate first decide what to advertise, then
 *	  enable/disable auto-negotiation as desired, and reset.
 *	- If the PHY does not auto-negotiate just reset it.
 *	- If auto-negotiation is off set the MAC to the proper speed/duplex/FC,
 *	  otherwise do it later based on the outcome of auto-negotiation.
 */
int t4_link_l1cfg(struct adapter *adap, unsigned int mbox, unsigned int port,
		  struct link_config *lc)
{
	struct fw_port_cmd c;
	unsigned int fc = 0, mdi = FW_PORT_CAP_MDI_V(FW_PORT_CAP_MDI_AUTO);

	lc->link_ok = 0;
	if (lc->requested_fc & PAUSE_RX)
		fc |= FW_PORT_CAP_FC_RX;
	if (lc->requested_fc & PAUSE_TX)
		fc |= FW_PORT_CAP_FC_TX;

	memset(&c, 0, sizeof(c));
	c.op_to_portid = cpu_to_be32(FW_CMD_OP_V(FW_PORT_CMD) |
				     FW_CMD_REQUEST_F | FW_CMD_EXEC_F |
				     FW_PORT_CMD_PORTID_V(port));
	c.action_to_len16 =
		cpu_to_be32(FW_PORT_CMD_ACTION_V(FW_PORT_ACTION_L1_CFG) |
			    FW_LEN16(c));

	if (!(lc->supported & FW_PORT_CAP_ANEG)) {
		c.u.l1cfg.rcap = cpu_to_be32((lc->supported & ADVERT_MASK) |
					     fc);
		lc->fc = lc->requested_fc & (PAUSE_RX | PAUSE_TX);
	} else if (lc->autoneg == AUTONEG_DISABLE) {
		c.u.l1cfg.rcap = cpu_to_be32(lc->requested_speed | fc | mdi);
		lc->fc = lc->requested_fc & (PAUSE_RX | PAUSE_TX);
	} else
		c.u.l1cfg.rcap = cpu_to_be32(lc->advertising | fc | mdi);

	return t4_wr_mbox(adap, mbox, &c, sizeof(c), NULL);
}

/**
 *	t4_restart_aneg - restart autonegotiation
 *	@adap: the adapter
 *	@mbox: mbox to use for the FW command
 *	@port: the port id
 *
 *	Restarts autonegotiation for the selected port.
 */
int t4_restart_aneg(struct adapter *adap, unsigned int mbox, unsigned int port)
{
	struct fw_port_cmd c;

	memset(&c, 0, sizeof(c));
	c.op_to_portid = cpu_to_be32(FW_CMD_OP_V(FW_PORT_CMD) |
				     FW_CMD_REQUEST_F | FW_CMD_EXEC_F |
				     FW_PORT_CMD_PORTID_V(port));
	c.action_to_len16 =
		cpu_to_be32(FW_PORT_CMD_ACTION_V(FW_PORT_ACTION_L1_CFG) |
			    FW_LEN16(c));
	c.u.l1cfg.rcap = cpu_to_be32(FW_PORT_CAP_ANEG);
	return t4_wr_mbox(adap, mbox, &c, sizeof(c), NULL);
}

typedef void (*int_handler_t)(struct adapter *adap);

struct intr_info {
	unsigned int mask;       /* bits to check in interrupt status */
	const char *msg;         /* message to print or NULL */
	short stat_idx;          /* stat counter to increment or -1 */
	unsigned short fatal;    /* whether the condition reported is fatal */
	int_handler_t int_handler; /* platform-specific int handler */
};

/**
 *	t4_handle_intr_status - table driven interrupt handler
 *	@adapter: the adapter that generated the interrupt
 *	@reg: the interrupt status register to process
 *	@acts: table of interrupt actions
 *
 *	A table driven interrupt handler that applies a set of masks to an
 *	interrupt status word and performs the corresponding actions if the
 *	interrupts described by the mask have occurred.  The actions include
 *	optionally emitting a warning or alert message.  The table is terminated
 *	by an entry specifying mask 0.  Returns the number of fatal interrupt
 *	conditions.
 */
static int t4_handle_intr_status(struct adapter *adapter, unsigned int reg,
				 const struct intr_info *acts)
{
	int fatal = 0;
	unsigned int mask = 0;
	unsigned int status = t4_read_reg(adapter, reg);

	for ( ; acts->mask; ++acts) {
		if (!(status & acts->mask))
			continue;
		if (acts->fatal) {
			fatal++;
			dev_alert(adapter->pdev_dev, "%s (0x%x)\n", acts->msg,
				  status & acts->mask);
		} else if (acts->msg && printk_ratelimit())
			dev_warn(adapter->pdev_dev, "%s (0x%x)\n", acts->msg,
				 status & acts->mask);
		if (acts->int_handler)
			acts->int_handler(adapter);
		mask |= acts->mask;
	}
	status &= mask;
	if (status)                           /* clear processed interrupts */
		t4_write_reg(adapter, reg, status);
	return fatal;
}

/*
 * Interrupt handler for the PCIE module.
 */
static void pcie_intr_handler(struct adapter *adapter)
{
	static const struct intr_info sysbus_intr_info[] = {
		{ RNPP_F, "RXNP array parity error", -1, 1 },
		{ RPCP_F, "RXPC array parity error", -1, 1 },
		{ RCIP_F, "RXCIF array parity error", -1, 1 },
		{ RCCP_F, "Rx completions control array parity error", -1, 1 },
		{ RFTP_F, "RXFT array parity error", -1, 1 },
		{ 0 }
	};
	static const struct intr_info pcie_port_intr_info[] = {
		{ TPCP_F, "TXPC array parity error", -1, 1 },
		{ TNPP_F, "TXNP array parity error", -1, 1 },
		{ TFTP_F, "TXFT array parity error", -1, 1 },
		{ TCAP_F, "TXCA array parity error", -1, 1 },
		{ TCIP_F, "TXCIF array parity error", -1, 1 },
		{ RCAP_F, "RXCA array parity error", -1, 1 },
		{ OTDD_F, "outbound request TLP discarded", -1, 1 },
		{ RDPE_F, "Rx data parity error", -1, 1 },
		{ TDUE_F, "Tx uncorrectable data error", -1, 1 },
		{ 0 }
	};
	static const struct intr_info pcie_intr_info[] = {
		{ MSIADDRLPERR_F, "MSI AddrL parity error", -1, 1 },
		{ MSIADDRHPERR_F, "MSI AddrH parity error", -1, 1 },
		{ MSIDATAPERR_F, "MSI data parity error", -1, 1 },
		{ MSIXADDRLPERR_F, "MSI-X AddrL parity error", -1, 1 },
		{ MSIXADDRHPERR_F, "MSI-X AddrH parity error", -1, 1 },
		{ MSIXDATAPERR_F, "MSI-X data parity error", -1, 1 },
		{ MSIXDIPERR_F, "MSI-X DI parity error", -1, 1 },
		{ PIOCPLPERR_F, "PCI PIO completion FIFO parity error", -1, 1 },
		{ PIOREQPERR_F, "PCI PIO request FIFO parity error", -1, 1 },
		{ TARTAGPERR_F, "PCI PCI target tag FIFO parity error", -1, 1 },
		{ CCNTPERR_F, "PCI CMD channel count parity error", -1, 1 },
		{ CREQPERR_F, "PCI CMD channel request parity error", -1, 1 },
		{ CRSPPERR_F, "PCI CMD channel response parity error", -1, 1 },
		{ DCNTPERR_F, "PCI DMA channel count parity error", -1, 1 },
		{ DREQPERR_F, "PCI DMA channel request parity error", -1, 1 },
		{ DRSPPERR_F, "PCI DMA channel response parity error", -1, 1 },
		{ HCNTPERR_F, "PCI HMA channel count parity error", -1, 1 },
		{ HREQPERR_F, "PCI HMA channel request parity error", -1, 1 },
		{ HRSPPERR_F, "PCI HMA channel response parity error", -1, 1 },
		{ CFGSNPPERR_F, "PCI config snoop FIFO parity error", -1, 1 },
		{ FIDPERR_F, "PCI FID parity error", -1, 1 },
		{ INTXCLRPERR_F, "PCI INTx clear parity error", -1, 1 },
		{ MATAGPERR_F, "PCI MA tag parity error", -1, 1 },
		{ PIOTAGPERR_F, "PCI PIO tag parity error", -1, 1 },
		{ RXCPLPERR_F, "PCI Rx completion parity error", -1, 1 },
		{ RXWRPERR_F, "PCI Rx write parity error", -1, 1 },
		{ RPLPERR_F, "PCI replay buffer parity error", -1, 1 },
		{ PCIESINT_F, "PCI core secondary fault", -1, 1 },
		{ PCIEPINT_F, "PCI core primary fault", -1, 1 },
		{ UNXSPLCPLERR_F, "PCI unexpected split completion error",
		  -1, 0 },
		{ 0 }
	};

	static struct intr_info t5_pcie_intr_info[] = {
		{ MSTGRPPERR_F, "Master Response Read Queue parity error",
		  -1, 1 },
		{ MSTTIMEOUTPERR_F, "Master Timeout FIFO parity error", -1, 1 },
		{ MSIXSTIPERR_F, "MSI-X STI SRAM parity error", -1, 1 },
		{ MSIXADDRLPERR_F, "MSI-X AddrL parity error", -1, 1 },
		{ MSIXADDRHPERR_F, "MSI-X AddrH parity error", -1, 1 },
		{ MSIXDATAPERR_F, "MSI-X data parity error", -1, 1 },
		{ MSIXDIPERR_F, "MSI-X DI parity error", -1, 1 },
		{ PIOCPLGRPPERR_F, "PCI PIO completion Group FIFO parity error",
		  -1, 1 },
		{ PIOREQGRPPERR_F, "PCI PIO request Group FIFO parity error",
		  -1, 1 },
		{ TARTAGPERR_F, "PCI PCI target tag FIFO parity error", -1, 1 },
		{ MSTTAGQPERR_F, "PCI master tag queue parity error", -1, 1 },
		{ CREQPERR_F, "PCI CMD channel request parity error", -1, 1 },
		{ CRSPPERR_F, "PCI CMD channel response parity error", -1, 1 },
		{ DREQWRPERR_F, "PCI DMA channel write request parity error",
		  -1, 1 },
		{ DREQPERR_F, "PCI DMA channel request parity error", -1, 1 },
		{ DRSPPERR_F, "PCI DMA channel response parity error", -1, 1 },
		{ HREQWRPERR_F, "PCI HMA channel count parity error", -1, 1 },
		{ HREQPERR_F, "PCI HMA channel request parity error", -1, 1 },
		{ HRSPPERR_F, "PCI HMA channel response parity error", -1, 1 },
		{ CFGSNPPERR_F, "PCI config snoop FIFO parity error", -1, 1 },
		{ FIDPERR_F, "PCI FID parity error", -1, 1 },
		{ VFIDPERR_F, "PCI INTx clear parity error", -1, 1 },
		{ MAGRPPERR_F, "PCI MA group FIFO parity error", -1, 1 },
		{ PIOTAGPERR_F, "PCI PIO tag parity error", -1, 1 },
		{ IPRXHDRGRPPERR_F, "PCI IP Rx header group parity error",
		  -1, 1 },
		{ IPRXDATAGRPPERR_F, "PCI IP Rx data group parity error",
		  -1, 1 },
		{ RPLPERR_F, "PCI IP replay buffer parity error", -1, 1 },
		{ IPSOTPERR_F, "PCI IP SOT buffer parity error", -1, 1 },
		{ TRGT1GRPPERR_F, "PCI TRGT1 group FIFOs parity error", -1, 1 },
		{ READRSPERR_F, "Outbound read error", -1, 0 },
		{ 0 }
	};

	int fat;

	if (is_t4(adapter->params.chip))
		fat = t4_handle_intr_status(adapter,
				PCIE_CORE_UTL_SYSTEM_BUS_AGENT_STATUS_A,
				sysbus_intr_info) +
			t4_handle_intr_status(adapter,
					PCIE_CORE_UTL_PCI_EXPRESS_PORT_STATUS_A,
					pcie_port_intr_info) +
			t4_handle_intr_status(adapter, PCIE_INT_CAUSE_A,
					      pcie_intr_info);
	else
		fat = t4_handle_intr_status(adapter, PCIE_INT_CAUSE_A,
					    t5_pcie_intr_info);

	if (fat)
		t4_fatal_err(adapter);
}

/*
 * TP interrupt handler.
 */
static void tp_intr_handler(struct adapter *adapter)
{
	static const struct intr_info tp_intr_info[] = {
		{ 0x3fffffff, "TP parity error", -1, 1 },
		{ FLMTXFLSTEMPTY_F, "TP out of Tx pages", -1, 1 },
		{ 0 }
	};

	if (t4_handle_intr_status(adapter, TP_INT_CAUSE_A, tp_intr_info))
		t4_fatal_err(adapter);
}

/*
 * SGE interrupt handler.
 */
static void sge_intr_handler(struct adapter *adapter)
{
	u64 v;
	u32 err;

	static const struct intr_info sge_intr_info[] = {
		{ ERR_CPL_EXCEED_IQE_SIZE_F,
		  "SGE received CPL exceeding IQE size", -1, 1 },
		{ ERR_INVALID_CIDX_INC_F,
		  "SGE GTS CIDX increment too large", -1, 0 },
		{ ERR_CPL_OPCODE_0_F, "SGE received 0-length CPL", -1, 0 },
		{ DBFIFO_LP_INT_F, NULL, -1, 0, t4_db_full },
		{ ERR_DATA_CPL_ON_HIGH_QID1_F | ERR_DATA_CPL_ON_HIGH_QID0_F,
		  "SGE IQID > 1023 received CPL for FL", -1, 0 },
		{ ERR_BAD_DB_PIDX3_F, "SGE DBP 3 pidx increment too large", -1,
		  0 },
		{ ERR_BAD_DB_PIDX2_F, "SGE DBP 2 pidx increment too large", -1,
		  0 },
		{ ERR_BAD_DB_PIDX1_F, "SGE DBP 1 pidx increment too large", -1,
		  0 },
		{ ERR_BAD_DB_PIDX0_F, "SGE DBP 0 pidx increment too large", -1,
		  0 },
		{ ERR_ING_CTXT_PRIO_F,
		  "SGE too many priority ingress contexts", -1, 0 },
		{ INGRESS_SIZE_ERR_F, "SGE illegal ingress QID", -1, 0 },
		{ EGRESS_SIZE_ERR_F, "SGE illegal egress QID", -1, 0 },
		{ 0 }
	};

	static struct intr_info t4t5_sge_intr_info[] = {
		{ ERR_DROPPED_DB_F, NULL, -1, 0, t4_db_dropped },
		{ DBFIFO_HP_INT_F, NULL, -1, 0, t4_db_full },
		{ ERR_EGR_CTXT_PRIO_F,
		  "SGE too many priority egress contexts", -1, 0 },
		{ 0 }
	};

	v = (u64)t4_read_reg(adapter, SGE_INT_CAUSE1_A) |
		((u64)t4_read_reg(adapter, SGE_INT_CAUSE2_A) << 32);
	if (v) {
		dev_alert(adapter->pdev_dev, "SGE parity error (%#llx)\n",
				(unsigned long long)v);
		t4_write_reg(adapter, SGE_INT_CAUSE1_A, v);
		t4_write_reg(adapter, SGE_INT_CAUSE2_A, v >> 32);
	}

	v |= t4_handle_intr_status(adapter, SGE_INT_CAUSE3_A, sge_intr_info);
	if (CHELSIO_CHIP_VERSION(adapter->params.chip) <= CHELSIO_T5)
		v |= t4_handle_intr_status(adapter, SGE_INT_CAUSE3_A,
					   t4t5_sge_intr_info);

	err = t4_read_reg(adapter, SGE_ERROR_STATS_A);
	if (err & ERROR_QID_VALID_F) {
		dev_err(adapter->pdev_dev, "SGE error for queue %u\n",
			ERROR_QID_G(err));
		if (err & UNCAPTURED_ERROR_F)
			dev_err(adapter->pdev_dev,
				"SGE UNCAPTURED_ERROR set (clearing)\n");
		t4_write_reg(adapter, SGE_ERROR_STATS_A, ERROR_QID_VALID_F |
			     UNCAPTURED_ERROR_F);
	}

	if (v != 0)
		t4_fatal_err(adapter);
}

#define CIM_OBQ_INTR (OBQULP0PARERR_F | OBQULP1PARERR_F | OBQULP2PARERR_F |\
		      OBQULP3PARERR_F | OBQSGEPARERR_F | OBQNCSIPARERR_F)
#define CIM_IBQ_INTR (IBQTP0PARERR_F | IBQTP1PARERR_F | IBQULPPARERR_F |\
		      IBQSGEHIPARERR_F | IBQSGELOPARERR_F | IBQNCSIPARERR_F)

/*
 * CIM interrupt handler.
 */
static void cim_intr_handler(struct adapter *adapter)
{
	static const struct intr_info cim_intr_info[] = {
		{ PREFDROPINT_F, "CIM control register prefetch drop", -1, 1 },
		{ CIM_OBQ_INTR, "CIM OBQ parity error", -1, 1 },
		{ CIM_IBQ_INTR, "CIM IBQ parity error", -1, 1 },
		{ MBUPPARERR_F, "CIM mailbox uP parity error", -1, 1 },
		{ MBHOSTPARERR_F, "CIM mailbox host parity error", -1, 1 },
		{ TIEQINPARERRINT_F, "CIM TIEQ outgoing parity error", -1, 1 },
		{ TIEQOUTPARERRINT_F, "CIM TIEQ incoming parity error", -1, 1 },
		{ 0 }
	};
	static const struct intr_info cim_upintr_info[] = {
		{ RSVDSPACEINT_F, "CIM reserved space access", -1, 1 },
		{ ILLTRANSINT_F, "CIM illegal transaction", -1, 1 },
		{ ILLWRINT_F, "CIM illegal write", -1, 1 },
		{ ILLRDINT_F, "CIM illegal read", -1, 1 },
		{ ILLRDBEINT_F, "CIM illegal read BE", -1, 1 },
		{ ILLWRBEINT_F, "CIM illegal write BE", -1, 1 },
		{ SGLRDBOOTINT_F, "CIM single read from boot space", -1, 1 },
		{ SGLWRBOOTINT_F, "CIM single write to boot space", -1, 1 },
		{ BLKWRBOOTINT_F, "CIM block write to boot space", -1, 1 },
		{ SGLRDFLASHINT_F, "CIM single read from flash space", -1, 1 },
		{ SGLWRFLASHINT_F, "CIM single write to flash space", -1, 1 },
		{ BLKWRFLASHINT_F, "CIM block write to flash space", -1, 1 },
		{ SGLRDEEPROMINT_F, "CIM single EEPROM read", -1, 1 },
		{ SGLWREEPROMINT_F, "CIM single EEPROM write", -1, 1 },
		{ BLKRDEEPROMINT_F, "CIM block EEPROM read", -1, 1 },
		{ BLKWREEPROMINT_F, "CIM block EEPROM write", -1, 1 },
		{ SGLRDCTLINT_F, "CIM single read from CTL space", -1, 1 },
		{ SGLWRCTLINT_F, "CIM single write to CTL space", -1, 1 },
		{ BLKRDCTLINT_F, "CIM block read from CTL space", -1, 1 },
		{ BLKWRCTLINT_F, "CIM block write to CTL space", -1, 1 },
		{ SGLRDPLINT_F, "CIM single read from PL space", -1, 1 },
		{ SGLWRPLINT_F, "CIM single write to PL space", -1, 1 },
		{ BLKRDPLINT_F, "CIM block read from PL space", -1, 1 },
		{ BLKWRPLINT_F, "CIM block write to PL space", -1, 1 },
		{ REQOVRLOOKUPINT_F, "CIM request FIFO overwrite", -1, 1 },
		{ RSPOVRLOOKUPINT_F, "CIM response FIFO overwrite", -1, 1 },
		{ TIMEOUTINT_F, "CIM PIF timeout", -1, 1 },
		{ TIMEOUTMAINT_F, "CIM PIF MA timeout", -1, 1 },
		{ 0 }
	};

	int fat;

	if (t4_read_reg(adapter, PCIE_FW_A) & PCIE_FW_ERR_F)
		t4_report_fw_error(adapter);

	fat = t4_handle_intr_status(adapter, CIM_HOST_INT_CAUSE_A,
				    cim_intr_info) +
	      t4_handle_intr_status(adapter, CIM_HOST_UPACC_INT_CAUSE_A,
				    cim_upintr_info);
	if (fat)
		t4_fatal_err(adapter);
}

/*
 * ULP RX interrupt handler.
 */
static void ulprx_intr_handler(struct adapter *adapter)
{
	static const struct intr_info ulprx_intr_info[] = {
		{ 0x1800000, "ULPRX context error", -1, 1 },
		{ 0x7fffff, "ULPRX parity error", -1, 1 },
		{ 0 }
	};

	if (t4_handle_intr_status(adapter, ULP_RX_INT_CAUSE_A, ulprx_intr_info))
		t4_fatal_err(adapter);
}

/*
 * ULP TX interrupt handler.
 */
static void ulptx_intr_handler(struct adapter *adapter)
{
	static const struct intr_info ulptx_intr_info[] = {
		{ PBL_BOUND_ERR_CH3_F, "ULPTX channel 3 PBL out of bounds", -1,
		  0 },
		{ PBL_BOUND_ERR_CH2_F, "ULPTX channel 2 PBL out of bounds", -1,
		  0 },
		{ PBL_BOUND_ERR_CH1_F, "ULPTX channel 1 PBL out of bounds", -1,
		  0 },
		{ PBL_BOUND_ERR_CH0_F, "ULPTX channel 0 PBL out of bounds", -1,
		  0 },
		{ 0xfffffff, "ULPTX parity error", -1, 1 },
		{ 0 }
	};

	if (t4_handle_intr_status(adapter, ULP_TX_INT_CAUSE_A, ulptx_intr_info))
		t4_fatal_err(adapter);
}

/*
 * PM TX interrupt handler.
 */
static void pmtx_intr_handler(struct adapter *adapter)
{
	static const struct intr_info pmtx_intr_info[] = {
		{ PCMD_LEN_OVFL0_F, "PMTX channel 0 pcmd too large", -1, 1 },
		{ PCMD_LEN_OVFL1_F, "PMTX channel 1 pcmd too large", -1, 1 },
		{ PCMD_LEN_OVFL2_F, "PMTX channel 2 pcmd too large", -1, 1 },
		{ ZERO_C_CMD_ERROR_F, "PMTX 0-length pcmd", -1, 1 },
		{ PMTX_FRAMING_ERROR_F, "PMTX framing error", -1, 1 },
		{ OESPI_PAR_ERROR_F, "PMTX oespi parity error", -1, 1 },
		{ DB_OPTIONS_PAR_ERROR_F, "PMTX db_options parity error",
		  -1, 1 },
		{ ICSPI_PAR_ERROR_F, "PMTX icspi parity error", -1, 1 },
		{ PMTX_C_PCMD_PAR_ERROR_F, "PMTX c_pcmd parity error", -1, 1},
		{ 0 }
	};

	if (t4_handle_intr_status(adapter, PM_TX_INT_CAUSE_A, pmtx_intr_info))
		t4_fatal_err(adapter);
}

/*
 * PM RX interrupt handler.
 */
static void pmrx_intr_handler(struct adapter *adapter)
{
	static const struct intr_info pmrx_intr_info[] = {
		{ ZERO_E_CMD_ERROR_F, "PMRX 0-length pcmd", -1, 1 },
		{ PMRX_FRAMING_ERROR_F, "PMRX framing error", -1, 1 },
		{ OCSPI_PAR_ERROR_F, "PMRX ocspi parity error", -1, 1 },
		{ DB_OPTIONS_PAR_ERROR_F, "PMRX db_options parity error",
		  -1, 1 },
		{ IESPI_PAR_ERROR_F, "PMRX iespi parity error", -1, 1 },
		{ PMRX_E_PCMD_PAR_ERROR_F, "PMRX e_pcmd parity error", -1, 1},
		{ 0 }
	};

	if (t4_handle_intr_status(adapter, PM_RX_INT_CAUSE_A, pmrx_intr_info))
		t4_fatal_err(adapter);
}

/*
 * CPL switch interrupt handler.
 */
static void cplsw_intr_handler(struct adapter *adapter)
{
	static const struct intr_info cplsw_intr_info[] = {
		{ CIM_OP_MAP_PERR_F, "CPLSW CIM op_map parity error", -1, 1 },
		{ CIM_OVFL_ERROR_F, "CPLSW CIM overflow", -1, 1 },
		{ TP_FRAMING_ERROR_F, "CPLSW TP framing error", -1, 1 },
		{ SGE_FRAMING_ERROR_F, "CPLSW SGE framing error", -1, 1 },
		{ CIM_FRAMING_ERROR_F, "CPLSW CIM framing error", -1, 1 },
		{ ZERO_SWITCH_ERROR_F, "CPLSW no-switch error", -1, 1 },
		{ 0 }
	};

	if (t4_handle_intr_status(adapter, CPL_INTR_CAUSE_A, cplsw_intr_info))
		t4_fatal_err(adapter);
}

/*
 * LE interrupt handler.
 */
static void le_intr_handler(struct adapter *adap)
{
	enum chip_type chip = CHELSIO_CHIP_VERSION(adap->params.chip);
	static const struct intr_info le_intr_info[] = {
		{ LIPMISS_F, "LE LIP miss", -1, 0 },
		{ LIP0_F, "LE 0 LIP error", -1, 0 },
		{ PARITYERR_F, "LE parity error", -1, 1 },
		{ UNKNOWNCMD_F, "LE unknown command", -1, 1 },
		{ REQQPARERR_F, "LE request queue parity error", -1, 1 },
		{ 0 }
	};

	static struct intr_info t6_le_intr_info[] = {
		{ T6_LIPMISS_F, "LE LIP miss", -1, 0 },
		{ T6_LIP0_F, "LE 0 LIP error", -1, 0 },
		{ TCAMINTPERR_F, "LE parity error", -1, 1 },
		{ T6_UNKNOWNCMD_F, "LE unknown command", -1, 1 },
		{ SSRAMINTPERR_F, "LE request queue parity error", -1, 1 },
		{ 0 }
	};

	if (t4_handle_intr_status(adap, LE_DB_INT_CAUSE_A,
				  (chip <= CHELSIO_T5) ?
				  le_intr_info : t6_le_intr_info))
		t4_fatal_err(adap);
}

/*
 * MPS interrupt handler.
 */
static void mps_intr_handler(struct adapter *adapter)
{
	static const struct intr_info mps_rx_intr_info[] = {
		{ 0xffffff, "MPS Rx parity error", -1, 1 },
		{ 0 }
	};
	static const struct intr_info mps_tx_intr_info[] = {
		{ TPFIFO_V(TPFIFO_M), "MPS Tx TP FIFO parity error", -1, 1 },
		{ NCSIFIFO_F, "MPS Tx NC-SI FIFO parity error", -1, 1 },
		{ TXDATAFIFO_V(TXDATAFIFO_M), "MPS Tx data FIFO parity error",
		  -1, 1 },
		{ TXDESCFIFO_V(TXDESCFIFO_M), "MPS Tx desc FIFO parity error",
		  -1, 1 },
		{ BUBBLE_F, "MPS Tx underflow", -1, 1 },
		{ SECNTERR_F, "MPS Tx SOP/EOP error", -1, 1 },
		{ FRMERR_F, "MPS Tx framing error", -1, 1 },
		{ 0 }
	};
	static const struct intr_info mps_trc_intr_info[] = {
		{ FILTMEM_V(FILTMEM_M), "MPS TRC filter parity error", -1, 1 },
		{ PKTFIFO_V(PKTFIFO_M), "MPS TRC packet FIFO parity error",
		  -1, 1 },
		{ MISCPERR_F, "MPS TRC misc parity error", -1, 1 },
		{ 0 }
	};
	static const struct intr_info mps_stat_sram_intr_info[] = {
		{ 0x1fffff, "MPS statistics SRAM parity error", -1, 1 },
		{ 0 }
	};
	static const struct intr_info mps_stat_tx_intr_info[] = {
		{ 0xfffff, "MPS statistics Tx FIFO parity error", -1, 1 },
		{ 0 }
	};
	static const struct intr_info mps_stat_rx_intr_info[] = {
		{ 0xffffff, "MPS statistics Rx FIFO parity error", -1, 1 },
		{ 0 }
	};
	static const struct intr_info mps_cls_intr_info[] = {
		{ MATCHSRAM_F, "MPS match SRAM parity error", -1, 1 },
		{ MATCHTCAM_F, "MPS match TCAM parity error", -1, 1 },
		{ HASHSRAM_F, "MPS hash SRAM parity error", -1, 1 },
		{ 0 }
	};

	int fat;

	fat = t4_handle_intr_status(adapter, MPS_RX_PERR_INT_CAUSE_A,
				    mps_rx_intr_info) +
	      t4_handle_intr_status(adapter, MPS_TX_INT_CAUSE_A,
				    mps_tx_intr_info) +
	      t4_handle_intr_status(adapter, MPS_TRC_INT_CAUSE_A,
				    mps_trc_intr_info) +
	      t4_handle_intr_status(adapter, MPS_STAT_PERR_INT_CAUSE_SRAM_A,
				    mps_stat_sram_intr_info) +
	      t4_handle_intr_status(adapter, MPS_STAT_PERR_INT_CAUSE_TX_FIFO_A,
				    mps_stat_tx_intr_info) +
	      t4_handle_intr_status(adapter, MPS_STAT_PERR_INT_CAUSE_RX_FIFO_A,
				    mps_stat_rx_intr_info) +
	      t4_handle_intr_status(adapter, MPS_CLS_INT_CAUSE_A,
				    mps_cls_intr_info);

	t4_write_reg(adapter, MPS_INT_CAUSE_A, 0);
	t4_read_reg(adapter, MPS_INT_CAUSE_A);                    /* flush */
	if (fat)
		t4_fatal_err(adapter);
}

#define MEM_INT_MASK (PERR_INT_CAUSE_F | ECC_CE_INT_CAUSE_F | \
		      ECC_UE_INT_CAUSE_F)

/*
 * EDC/MC interrupt handler.
 */
static void mem_intr_handler(struct adapter *adapter, int idx)
{
	static const char name[4][7] = { "EDC0", "EDC1", "MC/MC0", "MC1" };

	unsigned int addr, cnt_addr, v;

	if (idx <= MEM_EDC1) {
		addr = EDC_REG(EDC_INT_CAUSE_A, idx);
		cnt_addr = EDC_REG(EDC_ECC_STATUS_A, idx);
	} else if (idx == MEM_MC) {
		if (is_t4(adapter->params.chip)) {
			addr = MC_INT_CAUSE_A;
			cnt_addr = MC_ECC_STATUS_A;
		} else {
			addr = MC_P_INT_CAUSE_A;
			cnt_addr = MC_P_ECC_STATUS_A;
		}
	} else {
		addr = MC_REG(MC_P_INT_CAUSE_A, 1);
		cnt_addr = MC_REG(MC_P_ECC_STATUS_A, 1);
	}

	v = t4_read_reg(adapter, addr) & MEM_INT_MASK;
	if (v & PERR_INT_CAUSE_F)
		dev_alert(adapter->pdev_dev, "%s FIFO parity error\n",
			  name[idx]);
	if (v & ECC_CE_INT_CAUSE_F) {
		u32 cnt = ECC_CECNT_G(t4_read_reg(adapter, cnt_addr));

		t4_write_reg(adapter, cnt_addr, ECC_CECNT_V(ECC_CECNT_M));
		if (printk_ratelimit())
			dev_warn(adapter->pdev_dev,
				 "%u %s correctable ECC data error%s\n",
				 cnt, name[idx], cnt > 1 ? "s" : "");
	}
	if (v & ECC_UE_INT_CAUSE_F)
		dev_alert(adapter->pdev_dev,
			  "%s uncorrectable ECC data error\n", name[idx]);

	t4_write_reg(adapter, addr, v);
	if (v & (PERR_INT_CAUSE_F | ECC_UE_INT_CAUSE_F))
		t4_fatal_err(adapter);
}

/*
 * MA interrupt handler.
 */
static void ma_intr_handler(struct adapter *adap)
{
	u32 v, status = t4_read_reg(adap, MA_INT_CAUSE_A);

	if (status & MEM_PERR_INT_CAUSE_F) {
		dev_alert(adap->pdev_dev,
			  "MA parity error, parity status %#x\n",
			  t4_read_reg(adap, MA_PARITY_ERROR_STATUS1_A));
		if (is_t5(adap->params.chip))
			dev_alert(adap->pdev_dev,
				  "MA parity error, parity status %#x\n",
				  t4_read_reg(adap,
					      MA_PARITY_ERROR_STATUS2_A));
	}
	if (status & MEM_WRAP_INT_CAUSE_F) {
		v = t4_read_reg(adap, MA_INT_WRAP_STATUS_A);
		dev_alert(adap->pdev_dev, "MA address wrap-around error by "
			  "client %u to address %#x\n",
			  MEM_WRAP_CLIENT_NUM_G(v),
			  MEM_WRAP_ADDRESS_G(v) << 4);
	}
	t4_write_reg(adap, MA_INT_CAUSE_A, status);
	t4_fatal_err(adap);
}

/*
 * SMB interrupt handler.
 */
static void smb_intr_handler(struct adapter *adap)
{
	static const struct intr_info smb_intr_info[] = {
		{ MSTTXFIFOPARINT_F, "SMB master Tx FIFO parity error", -1, 1 },
		{ MSTRXFIFOPARINT_F, "SMB master Rx FIFO parity error", -1, 1 },
		{ SLVFIFOPARINT_F, "SMB slave FIFO parity error", -1, 1 },
		{ 0 }
	};

	if (t4_handle_intr_status(adap, SMB_INT_CAUSE_A, smb_intr_info))
		t4_fatal_err(adap);
}

/*
 * NC-SI interrupt handler.
 */
static void ncsi_intr_handler(struct adapter *adap)
{
	static const struct intr_info ncsi_intr_info[] = {
		{ CIM_DM_PRTY_ERR_F, "NC-SI CIM parity error", -1, 1 },
		{ MPS_DM_PRTY_ERR_F, "NC-SI MPS parity error", -1, 1 },
		{ TXFIFO_PRTY_ERR_F, "NC-SI Tx FIFO parity error", -1, 1 },
		{ RXFIFO_PRTY_ERR_F, "NC-SI Rx FIFO parity error", -1, 1 },
		{ 0 }
	};

	if (t4_handle_intr_status(adap, NCSI_INT_CAUSE_A, ncsi_intr_info))
		t4_fatal_err(adap);
}

/*
 * XGMAC interrupt handler.
 */
static void xgmac_intr_handler(struct adapter *adap, int port)
{
	u32 v, int_cause_reg;

	if (is_t4(adap->params.chip))
		int_cause_reg = PORT_REG(port, XGMAC_PORT_INT_CAUSE_A);
	else
		int_cause_reg = T5_PORT_REG(port, MAC_PORT_INT_CAUSE_A);

	v = t4_read_reg(adap, int_cause_reg);

	v &= TXFIFO_PRTY_ERR_F | RXFIFO_PRTY_ERR_F;
	if (!v)
		return;

	if (v & TXFIFO_PRTY_ERR_F)
		dev_alert(adap->pdev_dev, "XGMAC %d Tx FIFO parity error\n",
			  port);
	if (v & RXFIFO_PRTY_ERR_F)
		dev_alert(adap->pdev_dev, "XGMAC %d Rx FIFO parity error\n",
			  port);
	t4_write_reg(adap, PORT_REG(port, XGMAC_PORT_INT_CAUSE_A), v);
	t4_fatal_err(adap);
}

/*
 * PL interrupt handler.
 */
static void pl_intr_handler(struct adapter *adap)
{
	static const struct intr_info pl_intr_info[] = {
		{ FATALPERR_F, "T4 fatal parity error", -1, 1 },
		{ PERRVFID_F, "PL VFID_MAP parity error", -1, 1 },
		{ 0 }
	};

	if (t4_handle_intr_status(adap, PL_PL_INT_CAUSE_A, pl_intr_info))
		t4_fatal_err(adap);
}

#define PF_INTR_MASK (PFSW_F)
#define GLBL_INTR_MASK (CIM_F | MPS_F | PL_F | PCIE_F | MC_F | EDC0_F | \
		EDC1_F | LE_F | TP_F | MA_F | PM_TX_F | PM_RX_F | ULP_RX_F | \
		CPL_SWITCH_F | SGE_F | ULP_TX_F)

/**
 *	t4_slow_intr_handler - control path interrupt handler
 *	@adapter: the adapter
 *
 *	T4 interrupt handler for non-data global interrupt events, e.g., errors.
 *	The designation 'slow' is because it involves register reads, while
 *	data interrupts typically don't involve any MMIOs.
 */
int t4_slow_intr_handler(struct adapter *adapter)
{
	u32 cause = t4_read_reg(adapter, PL_INT_CAUSE_A);

	if (!(cause & GLBL_INTR_MASK))
		return 0;
	if (cause & CIM_F)
		cim_intr_handler(adapter);
	if (cause & MPS_F)
		mps_intr_handler(adapter);
	if (cause & NCSI_F)
		ncsi_intr_handler(adapter);
	if (cause & PL_F)
		pl_intr_handler(adapter);
	if (cause & SMB_F)
		smb_intr_handler(adapter);
	if (cause & XGMAC0_F)
		xgmac_intr_handler(adapter, 0);
	if (cause & XGMAC1_F)
		xgmac_intr_handler(adapter, 1);
	if (cause & XGMAC_KR0_F)
		xgmac_intr_handler(adapter, 2);
	if (cause & XGMAC_KR1_F)
		xgmac_intr_handler(adapter, 3);
	if (cause & PCIE_F)
		pcie_intr_handler(adapter);
	if (cause & MC_F)
		mem_intr_handler(adapter, MEM_MC);
	if (is_t5(adapter->params.chip) && (cause & MC1_F))
		mem_intr_handler(adapter, MEM_MC1);
	if (cause & EDC0_F)
		mem_intr_handler(adapter, MEM_EDC0);
	if (cause & EDC1_F)
		mem_intr_handler(adapter, MEM_EDC1);
	if (cause & LE_F)
		le_intr_handler(adapter);
	if (cause & TP_F)
		tp_intr_handler(adapter);
	if (cause & MA_F)
		ma_intr_handler(adapter);
	if (cause & PM_TX_F)
		pmtx_intr_handler(adapter);
	if (cause & PM_RX_F)
		pmrx_intr_handler(adapter);
	if (cause & ULP_RX_F)
		ulprx_intr_handler(adapter);
	if (cause & CPL_SWITCH_F)
		cplsw_intr_handler(adapter);
	if (cause & SGE_F)
		sge_intr_handler(adapter);
	if (cause & ULP_TX_F)
		ulptx_intr_handler(adapter);

	/* Clear the interrupts just processed for which we are the master. */
	t4_write_reg(adapter, PL_INT_CAUSE_A, cause & GLBL_INTR_MASK);
	(void)t4_read_reg(adapter, PL_INT_CAUSE_A); /* flush */
	return 1;
}

/**
 *	t4_intr_enable - enable interrupts
 *	@adapter: the adapter whose interrupts should be enabled
 *
 *	Enable PF-specific interrupts for the calling function and the top-level
 *	interrupt concentrator for global interrupts.  Interrupts are already
 *	enabled at each module,	here we just enable the roots of the interrupt
 *	hierarchies.
 *
 *	Note: this function should be called only when the driver manages
 *	non PF-specific interrupts from the various HW modules.  Only one PCI
 *	function at a time should be doing this.
 */
void t4_intr_enable(struct adapter *adapter)
{
	u32 val = 0;
	u32 pf = SOURCEPF_G(t4_read_reg(adapter, PL_WHOAMI_A));

	if (CHELSIO_CHIP_VERSION(adapter->params.chip) <= CHELSIO_T5)
		val = ERR_DROPPED_DB_F | ERR_EGR_CTXT_PRIO_F | DBFIFO_HP_INT_F;
	t4_write_reg(adapter, SGE_INT_ENABLE3_A, ERR_CPL_EXCEED_IQE_SIZE_F |
		     ERR_INVALID_CIDX_INC_F | ERR_CPL_OPCODE_0_F |
		     ERR_DATA_CPL_ON_HIGH_QID1_F | INGRESS_SIZE_ERR_F |
		     ERR_DATA_CPL_ON_HIGH_QID0_F | ERR_BAD_DB_PIDX3_F |
		     ERR_BAD_DB_PIDX2_F | ERR_BAD_DB_PIDX1_F |
		     ERR_BAD_DB_PIDX0_F | ERR_ING_CTXT_PRIO_F |
		     DBFIFO_LP_INT_F | EGRESS_SIZE_ERR_F | val);
	t4_write_reg(adapter, MYPF_REG(PL_PF_INT_ENABLE_A), PF_INTR_MASK);
	t4_set_reg_field(adapter, PL_INT_MAP0_A, 0, 1 << pf);
}

/**
 *	t4_intr_disable - disable interrupts
 *	@adapter: the adapter whose interrupts should be disabled
 *
 *	Disable interrupts.  We only disable the top-level interrupt
 *	concentrators.  The caller must be a PCI function managing global
 *	interrupts.
 */
void t4_intr_disable(struct adapter *adapter)
{
	u32 pf = SOURCEPF_G(t4_read_reg(adapter, PL_WHOAMI_A));

	t4_write_reg(adapter, MYPF_REG(PL_PF_INT_ENABLE_A), 0);
	t4_set_reg_field(adapter, PL_INT_MAP0_A, 1 << pf, 0);
}

/**
 *	hash_mac_addr - return the hash value of a MAC address
 *	@addr: the 48-bit Ethernet MAC address
 *
 *	Hashes a MAC address according to the hash function used by HW inexact
 *	(hash) address matching.
 */
static int hash_mac_addr(const u8 *addr)
{
	u32 a = ((u32)addr[0] << 16) | ((u32)addr[1] << 8) | addr[2];
	u32 b = ((u32)addr[3] << 16) | ((u32)addr[4] << 8) | addr[5];
	a ^= b;
	a ^= (a >> 12);
	a ^= (a >> 6);
	return a & 0x3f;
}

/**
 *	t4_config_rss_range - configure a portion of the RSS mapping table
 *	@adapter: the adapter
 *	@mbox: mbox to use for the FW command
 *	@viid: virtual interface whose RSS subtable is to be written
 *	@start: start entry in the table to write
 *	@n: how many table entries to write
 *	@rspq: values for the response queue lookup table
 *	@nrspq: number of values in @rspq
 *
 *	Programs the selected part of the VI's RSS mapping table with the
 *	provided values.  If @nrspq < @n the supplied values are used repeatedly
 *	until the full table range is populated.
 *
 *	The caller must ensure the values in @rspq are in the range allowed for
 *	@viid.
 */
int t4_config_rss_range(struct adapter *adapter, int mbox, unsigned int viid,
			int start, int n, const u16 *rspq, unsigned int nrspq)
{
	int ret;
	const u16 *rsp = rspq;
	const u16 *rsp_end = rspq + nrspq;
	struct fw_rss_ind_tbl_cmd cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.op_to_viid = cpu_to_be32(FW_CMD_OP_V(FW_RSS_IND_TBL_CMD) |
			       FW_CMD_REQUEST_F | FW_CMD_WRITE_F |
			       FW_RSS_IND_TBL_CMD_VIID_V(viid));
	cmd.retval_len16 = cpu_to_be32(FW_LEN16(cmd));

	/* each fw_rss_ind_tbl_cmd takes up to 32 entries */
	while (n > 0) {
		int nq = min(n, 32);
		__be32 *qp = &cmd.iq0_to_iq2;

		cmd.niqid = cpu_to_be16(nq);
		cmd.startidx = cpu_to_be16(start);

		start += nq;
		n -= nq;

		while (nq > 0) {
			unsigned int v;

			v = FW_RSS_IND_TBL_CMD_IQ0_V(*rsp);
			if (++rsp >= rsp_end)
				rsp = rspq;
			v |= FW_RSS_IND_TBL_CMD_IQ1_V(*rsp);
			if (++rsp >= rsp_end)
				rsp = rspq;
			v |= FW_RSS_IND_TBL_CMD_IQ2_V(*rsp);
			if (++rsp >= rsp_end)
				rsp = rspq;

			*qp++ = cpu_to_be32(v);
			nq -= 3;
		}

		ret = t4_wr_mbox(adapter, mbox, &cmd, sizeof(cmd), NULL);
		if (ret)
			return ret;
	}
	return 0;
}

/**
 *	t4_config_glbl_rss - configure the global RSS mode
 *	@adapter: the adapter
 *	@mbox: mbox to use for the FW command
 *	@mode: global RSS mode
 *	@flags: mode-specific flags
 *
 *	Sets the global RSS mode.
 */
int t4_config_glbl_rss(struct adapter *adapter, int mbox, unsigned int mode,
		       unsigned int flags)
{
	struct fw_rss_glb_config_cmd c;

	memset(&c, 0, sizeof(c));
	c.op_to_write = cpu_to_be32(FW_CMD_OP_V(FW_RSS_GLB_CONFIG_CMD) |
				    FW_CMD_REQUEST_F | FW_CMD_WRITE_F);
	c.retval_len16 = cpu_to_be32(FW_LEN16(c));
	if (mode == FW_RSS_GLB_CONFIG_CMD_MODE_MANUAL) {
		c.u.manual.mode_pkd =
			cpu_to_be32(FW_RSS_GLB_CONFIG_CMD_MODE_V(mode));
	} else if (mode == FW_RSS_GLB_CONFIG_CMD_MODE_BASICVIRTUAL) {
		c.u.basicvirtual.mode_pkd =
			cpu_to_be32(FW_RSS_GLB_CONFIG_CMD_MODE_V(mode));
		c.u.basicvirtual.synmapen_to_hashtoeplitz = cpu_to_be32(flags);
	} else
		return -EINVAL;
	return t4_wr_mbox(adapter, mbox, &c, sizeof(c), NULL);
}

/**
 *	t4_config_vi_rss - configure per VI RSS settings
 *	@adapter: the adapter
 *	@mbox: mbox to use for the FW command
 *	@viid: the VI id
 *	@flags: RSS flags
 *	@defq: id of the default RSS queue for the VI.
 *
 *	Configures VI-specific RSS properties.
 */
int t4_config_vi_rss(struct adapter *adapter, int mbox, unsigned int viid,
		     unsigned int flags, unsigned int defq)
{
	struct fw_rss_vi_config_cmd c;

	memset(&c, 0, sizeof(c));
	c.op_to_viid = cpu_to_be32(FW_CMD_OP_V(FW_RSS_VI_CONFIG_CMD) |
				   FW_CMD_REQUEST_F | FW_CMD_WRITE_F |
				   FW_RSS_VI_CONFIG_CMD_VIID_V(viid));
	c.retval_len16 = cpu_to_be32(FW_LEN16(c));
	c.u.basicvirtual.defaultq_to_udpen = cpu_to_be32(flags |
					FW_RSS_VI_CONFIG_CMD_DEFAULTQ_V(defq));
	return t4_wr_mbox(adapter, mbox, &c, sizeof(c), NULL);
}

/* Read an RSS table row */
static int rd_rss_row(struct adapter *adap, int row, u32 *val)
{
	t4_write_reg(adap, TP_RSS_LKP_TABLE_A, 0xfff00000 | row);
	return t4_wait_op_done_val(adap, TP_RSS_LKP_TABLE_A, LKPTBLROWVLD_F, 1,
				   5, 0, val);
}

/**
 *	t4_read_rss - read the contents of the RSS mapping table
 *	@adapter: the adapter
 *	@map: holds the contents of the RSS mapping table
 *
 *	Reads the contents of the RSS hash->queue mapping table.
 */
int t4_read_rss(struct adapter *adapter, u16 *map)
{
	u32 val;
	int i, ret;

	for (i = 0; i < RSS_NENTRIES / 2; ++i) {
		ret = rd_rss_row(adapter, i, &val);
		if (ret)
			return ret;
		*map++ = LKPTBLQUEUE0_G(val);
		*map++ = LKPTBLQUEUE1_G(val);
	}
	return 0;
}

static unsigned int t4_use_ldst(struct adapter *adap)
{
	return (adap->flags & FW_OK) || !adap->use_bd;
}

/**
 *	t4_fw_tp_pio_rw - Access TP PIO through LDST
 *	@adap: the adapter
 *	@vals: where the indirect register values are stored/written
 *	@nregs: how many indirect registers to read/write
 *	@start_idx: index of first indirect register to read/write
 *	@rw: Read (1) or Write (0)
 *
 *	Access TP PIO registers through LDST
 */
static void t4_fw_tp_pio_rw(struct adapter *adap, u32 *vals, unsigned int nregs,
			    unsigned int start_index, unsigned int rw)
{
	int ret, i;
	int cmd = FW_LDST_ADDRSPC_TP_PIO;
	struct fw_ldst_cmd c;

	for (i = 0 ; i < nregs; i++) {
		memset(&c, 0, sizeof(c));
		c.op_to_addrspace = cpu_to_be32(FW_CMD_OP_V(FW_LDST_CMD) |
						FW_CMD_REQUEST_F |
						(rw ? FW_CMD_READ_F :
						      FW_CMD_WRITE_F) |
						FW_LDST_CMD_ADDRSPACE_V(cmd));
		c.cycles_to_len16 = cpu_to_be32(FW_LEN16(c));

		c.u.addrval.addr = cpu_to_be32(start_index + i);
		c.u.addrval.val  = rw ? 0 : cpu_to_be32(vals[i]);
		ret = t4_wr_mbox(adap, adap->mbox, &c, sizeof(c), &c);
		if (!ret && rw)
			vals[i] = be32_to_cpu(c.u.addrval.val);
	}
}

/**
 *	t4_read_rss_key - read the global RSS key
 *	@adap: the adapter
 *	@key: 10-entry array holding the 320-bit RSS key
 *
 *	Reads the global 320-bit RSS key.
 */
void t4_read_rss_key(struct adapter *adap, u32 *key)
{
	if (t4_use_ldst(adap))
		t4_fw_tp_pio_rw(adap, key, 10, TP_RSS_SECRET_KEY0_A, 1);
	else
		t4_read_indirect(adap, TP_PIO_ADDR_A, TP_PIO_DATA_A, key, 10,
				 TP_RSS_SECRET_KEY0_A);
}

/**
 *	t4_write_rss_key - program one of the RSS keys
 *	@adap: the adapter
 *	@key: 10-entry array holding the 320-bit RSS key
 *	@idx: which RSS key to write
 *
 *	Writes one of the RSS keys with the given 320-bit value.  If @idx is
 *	0..15 the corresponding entry in the RSS key table is written,
 *	otherwise the global RSS key is written.
 */
void t4_write_rss_key(struct adapter *adap, const u32 *key, int idx)
{
	u8 rss_key_addr_cnt = 16;
	u32 vrt = t4_read_reg(adap, TP_RSS_CONFIG_VRT_A);

	/* T6 and later: for KeyMode 3 (per-vf and per-vf scramble),
	 * allows access to key addresses 16-63 by using KeyWrAddrX
	 * as index[5:4](upper 2) into key table
	 */
	if ((CHELSIO_CHIP_VERSION(adap->params.chip) > CHELSIO_T5) &&
	    (vrt & KEYEXTEND_F) && (KEYMODE_G(vrt) == 3))
		rss_key_addr_cnt = 32;

	if (t4_use_ldst(adap))
		t4_fw_tp_pio_rw(adap, (void *)key, 10, TP_RSS_SECRET_KEY0_A, 0);
	else
		t4_write_indirect(adap, TP_PIO_ADDR_A, TP_PIO_DATA_A, key, 10,
				  TP_RSS_SECRET_KEY0_A);

	if (idx >= 0 && idx < rss_key_addr_cnt) {
		if (rss_key_addr_cnt > 16)
			t4_write_reg(adap, TP_RSS_CONFIG_VRT_A,
				     KEYWRADDRX_V(idx >> 4) |
				     T6_VFWRADDR_V(idx) | KEYWREN_F);
		else
			t4_write_reg(adap, TP_RSS_CONFIG_VRT_A,
				     KEYWRADDR_V(idx) | KEYWREN_F);
	}
}

/**
 *	t4_read_rss_pf_config - read PF RSS Configuration Table
 *	@adapter: the adapter
 *	@index: the entry in the PF RSS table to read
 *	@valp: where to store the returned value
 *
 *	Reads the PF RSS Configuration Table at the specified index and returns
 *	the value found there.
 */
void t4_read_rss_pf_config(struct adapter *adapter, unsigned int index,
			   u32 *valp)
{
	if (t4_use_ldst(adapter))
		t4_fw_tp_pio_rw(adapter, valp, 1,
				TP_RSS_PF0_CONFIG_A + index, 1);
	else
		t4_read_indirect(adapter, TP_PIO_ADDR_A, TP_PIO_DATA_A,
				 valp, 1, TP_RSS_PF0_CONFIG_A + index);
}

/**
 *	t4_read_rss_vf_config - read VF RSS Configuration Table
 *	@adapter: the adapter
 *	@index: the entry in the VF RSS table to read
 *	@vfl: where to store the returned VFL
 *	@vfh: where to store the returned VFH
 *
 *	Reads the VF RSS Configuration Table at the specified index and returns
 *	the (VFL, VFH) values found there.
 */
void t4_read_rss_vf_config(struct adapter *adapter, unsigned int index,
			   u32 *vfl, u32 *vfh)
{
	u32 vrt, mask, data;

	if (CHELSIO_CHIP_VERSION(adapter->params.chip) <= CHELSIO_T5) {
		mask = VFWRADDR_V(VFWRADDR_M);
		data = VFWRADDR_V(index);
	} else {
		 mask =  T6_VFWRADDR_V(T6_VFWRADDR_M);
		 data = T6_VFWRADDR_V(index);
	}

	/* Request that the index'th VF Table values be read into VFL/VFH.
	 */
	vrt = t4_read_reg(adapter, TP_RSS_CONFIG_VRT_A);
	vrt &= ~(VFRDRG_F | VFWREN_F | KEYWREN_F | mask);
	vrt |= data | VFRDEN_F;
	t4_write_reg(adapter, TP_RSS_CONFIG_VRT_A, vrt);

	/* Grab the VFL/VFH values ...
	 */
	if (t4_use_ldst(adapter)) {
		t4_fw_tp_pio_rw(adapter, vfl, 1, TP_RSS_VFL_CONFIG_A, 1);
		t4_fw_tp_pio_rw(adapter, vfh, 1, TP_RSS_VFH_CONFIG_A, 1);
	} else {
		t4_read_indirect(adapter, TP_PIO_ADDR_A, TP_PIO_DATA_A,
				 vfl, 1, TP_RSS_VFL_CONFIG_A);
		t4_read_indirect(adapter, TP_PIO_ADDR_A, TP_PIO_DATA_A,
				 vfh, 1, TP_RSS_VFH_CONFIG_A);
	}
}

/**
 *	t4_read_rss_pf_map - read PF RSS Map
 *	@adapter: the adapter
 *
 *	Reads the PF RSS Map register and returns its value.
 */
u32 t4_read_rss_pf_map(struct adapter *adapter)
{
	u32 pfmap;

	if (t4_use_ldst(adapter))
		t4_fw_tp_pio_rw(adapter, &pfmap, 1, TP_RSS_PF_MAP_A, 1);
	else
		t4_read_indirect(adapter, TP_PIO_ADDR_A, TP_PIO_DATA_A,
				 &pfmap, 1, TP_RSS_PF_MAP_A);
	return pfmap;
}

/**
 *	t4_read_rss_pf_mask - read PF RSS Mask
 *	@adapter: the adapter
 *
 *	Reads the PF RSS Mask register and returns its value.
 */
u32 t4_read_rss_pf_mask(struct adapter *adapter)
{
	u32 pfmask;

	if (t4_use_ldst(adapter))
		t4_fw_tp_pio_rw(adapter, &pfmask, 1, TP_RSS_PF_MSK_A, 1);
	else
		t4_read_indirect(adapter, TP_PIO_ADDR_A, TP_PIO_DATA_A,
				 &pfmask, 1, TP_RSS_PF_MSK_A);
	return pfmask;
}

/**
 *	t4_tp_get_tcp_stats - read TP's TCP MIB counters
 *	@adap: the adapter
 *	@v4: holds the TCP/IP counter values
 *	@v6: holds the TCP/IPv6 counter values
 *
 *	Returns the values of TP's TCP/IP and TCP/IPv6 MIB counters.
 *	Either @v4 or @v6 may be %NULL to skip the corresponding stats.
 */
void t4_tp_get_tcp_stats(struct adapter *adap, struct tp_tcp_stats *v4,
			 struct tp_tcp_stats *v6)
{
	u32 val[TP_MIB_TCP_RXT_SEG_LO_A - TP_MIB_TCP_OUT_RST_A + 1];

#define STAT_IDX(x) ((TP_MIB_TCP_##x##_A) - TP_MIB_TCP_OUT_RST_A)
#define STAT(x)     val[STAT_IDX(x)]
#define STAT64(x)   (((u64)STAT(x##_HI) << 32) | STAT(x##_LO))

	if (v4) {
		t4_read_indirect(adap, TP_MIB_INDEX_A, TP_MIB_DATA_A, val,
				 ARRAY_SIZE(val), TP_MIB_TCP_OUT_RST_A);
		v4->tcp_out_rsts = STAT(OUT_RST);
		v4->tcp_in_segs  = STAT64(IN_SEG);
		v4->tcp_out_segs = STAT64(OUT_SEG);
		v4->tcp_retrans_segs = STAT64(RXT_SEG);
	}
	if (v6) {
		t4_read_indirect(adap, TP_MIB_INDEX_A, TP_MIB_DATA_A, val,
				 ARRAY_SIZE(val), TP_MIB_TCP_V6OUT_RST_A);
		v6->tcp_out_rsts = STAT(OUT_RST);
		v6->tcp_in_segs  = STAT64(IN_SEG);
		v6->tcp_out_segs = STAT64(OUT_SEG);
		v6->tcp_retrans_segs = STAT64(RXT_SEG);
	}
#undef STAT64
#undef STAT
#undef STAT_IDX
}

/**
 *	t4_tp_get_err_stats - read TP's error MIB counters
 *	@adap: the adapter
 *	@st: holds the counter values
 *
 *	Returns the values of TP's error counters.
 */
void t4_tp_get_err_stats(struct adapter *adap, struct tp_err_stats *st)
{
	int nchan = adap->params.arch.nchan;

	t4_read_indirect(adap, TP_MIB_INDEX_A, TP_MIB_DATA_A,
			 st->mac_in_errs, nchan, TP_MIB_MAC_IN_ERR_0_A);
	t4_read_indirect(adap, TP_MIB_INDEX_A, TP_MIB_DATA_A,
			 st->hdr_in_errs, nchan, TP_MIB_HDR_IN_ERR_0_A);
	t4_read_indirect(adap, TP_MIB_INDEX_A, TP_MIB_DATA_A,
			 st->tcp_in_errs, nchan, TP_MIB_TCP_IN_ERR_0_A);
	t4_read_indirect(adap, TP_MIB_INDEX_A, TP_MIB_DATA_A,
			 st->tnl_cong_drops, nchan, TP_MIB_TNL_CNG_DROP_0_A);
	t4_read_indirect(adap, TP_MIB_INDEX_A, TP_MIB_DATA_A,
			 st->ofld_chan_drops, nchan, TP_MIB_OFD_CHN_DROP_0_A);
	t4_read_indirect(adap, TP_MIB_INDEX_A, TP_MIB_DATA_A,
			 st->tnl_tx_drops, nchan, TP_MIB_TNL_DROP_0_A);
	t4_read_indirect(adap, TP_MIB_INDEX_A, TP_MIB_DATA_A,
			 st->ofld_vlan_drops, nchan, TP_MIB_OFD_VLN_DROP_0_A);
	t4_read_indirect(adap, TP_MIB_INDEX_A, TP_MIB_DATA_A,
			 st->tcp6_in_errs, nchan, TP_MIB_TCP_V6IN_ERR_0_A);

	t4_read_indirect(adap, TP_MIB_INDEX_A, TP_MIB_DATA_A,
			 &st->ofld_no_neigh, 2, TP_MIB_OFD_ARP_DROP_A);
}

/**
 *	t4_tp_get_cpl_stats - read TP's CPL MIB counters
 *	@adap: the adapter
 *	@st: holds the counter values
 *
 *	Returns the values of TP's CPL counters.
 */
void t4_tp_get_cpl_stats(struct adapter *adap, struct tp_cpl_stats *st)
{
	int nchan = adap->params.arch.nchan;

	t4_read_indirect(adap, TP_MIB_INDEX_A, TP_MIB_DATA_A, st->req,
			 nchan, TP_MIB_CPL_IN_REQ_0_A);
	t4_read_indirect(adap, TP_MIB_INDEX_A, TP_MIB_DATA_A, st->rsp,
			 nchan, TP_MIB_CPL_OUT_RSP_0_A);

}

/**
 *	t4_tp_get_rdma_stats - read TP's RDMA MIB counters
 *	@adap: the adapter
 *	@st: holds the counter values
 *
 *	Returns the values of TP's RDMA counters.
 */
void t4_tp_get_rdma_stats(struct adapter *adap, struct tp_rdma_stats *st)
{
	t4_read_indirect(adap, TP_MIB_INDEX_A, TP_MIB_DATA_A, &st->rqe_dfr_pkt,
			 2, TP_MIB_RQE_DFR_PKT_A);
}

/**
 *	t4_get_fcoe_stats - read TP's FCoE MIB counters for a port
 *	@adap: the adapter
 *	@idx: the port index
 *	@st: holds the counter values
 *
 *	Returns the values of TP's FCoE counters for the selected port.
 */
void t4_get_fcoe_stats(struct adapter *adap, unsigned int idx,
		       struct tp_fcoe_stats *st)
{
	u32 val[2];

	t4_read_indirect(adap, TP_MIB_INDEX_A, TP_MIB_DATA_A, &st->frames_ddp,
			 1, TP_MIB_FCOE_DDP_0_A + idx);
	t4_read_indirect(adap, TP_MIB_INDEX_A, TP_MIB_DATA_A, &st->frames_drop,
			 1, TP_MIB_FCOE_DROP_0_A + idx);
	t4_read_indirect(adap, TP_MIB_INDEX_A, TP_MIB_DATA_A, val,
			 2, TP_MIB_FCOE_BYTE_0_HI_A + 2 * idx);
	st->octets_ddp = ((u64)val[0] << 32) | val[1];
}

/**
 *	t4_get_usm_stats - read TP's non-TCP DDP MIB counters
 *	@adap: the adapter
 *	@st: holds the counter values
 *
 *	Returns the values of TP's counters for non-TCP directly-placed packets.
 */
void t4_get_usm_stats(struct adapter *adap, struct tp_usm_stats *st)
{
	u32 val[4];

	t4_read_indirect(adap, TP_MIB_INDEX_A, TP_MIB_DATA_A, val, 4,
			 TP_MIB_USM_PKTS_A);
	st->frames = val[0];
	st->drops = val[1];
	st->octets = ((u64)val[2] << 32) | val[3];
}

/**
 *	t4_read_mtu_tbl - returns the values in the HW path MTU table
 *	@adap: the adapter
 *	@mtus: where to store the MTU values
 *	@mtu_log: where to store the MTU base-2 log (may be %NULL)
 *
 *	Reads the HW path MTU table.
 */
void t4_read_mtu_tbl(struct adapter *adap, u16 *mtus, u8 *mtu_log)
{
	u32 v;
	int i;

	for (i = 0; i < NMTUS; ++i) {
		t4_write_reg(adap, TP_MTU_TABLE_A,
			     MTUINDEX_V(0xff) | MTUVALUE_V(i));
		v = t4_read_reg(adap, TP_MTU_TABLE_A);
		mtus[i] = MTUVALUE_G(v);
		if (mtu_log)
			mtu_log[i] = MTUWIDTH_G(v);
	}
}

/**
 *	t4_read_cong_tbl - reads the congestion control table
 *	@adap: the adapter
 *	@incr: where to store the alpha values
 *
 *	Reads the additive increments programmed into the HW congestion
 *	control table.
 */
void t4_read_cong_tbl(struct adapter *adap, u16 incr[NMTUS][NCCTRL_WIN])
{
	unsigned int mtu, w;

	for (mtu = 0; mtu < NMTUS; ++mtu)
		for (w = 0; w < NCCTRL_WIN; ++w) {
			t4_write_reg(adap, TP_CCTRL_TABLE_A,
				     ROWINDEX_V(0xffff) | (mtu << 5) | w);
			incr[mtu][w] = (u16)t4_read_reg(adap,
						TP_CCTRL_TABLE_A) & 0x1fff;
		}
}

/**
 *	t4_tp_wr_bits_indirect - set/clear bits in an indirect TP register
 *	@adap: the adapter
 *	@addr: the indirect TP register address
 *	@mask: specifies the field within the register to modify
 *	@val: new value for the field
 *
 *	Sets a field of an indirect TP register to the given value.
 */
void t4_tp_wr_bits_indirect(struct adapter *adap, unsigned int addr,
			    unsigned int mask, unsigned int val)
{
	t4_write_reg(adap, TP_PIO_ADDR_A, addr);
	val |= t4_read_reg(adap, TP_PIO_DATA_A) & ~mask;
	t4_write_reg(adap, TP_PIO_DATA_A, val);
}

/**
 *	init_cong_ctrl - initialize congestion control parameters
 *	@a: the alpha values for congestion control
 *	@b: the beta values for congestion control
 *
 *	Initialize the congestion control parameters.
 */
static void init_cong_ctrl(unsigned short *a, unsigned short *b)
{
	a[0] = a[1] = a[2] = a[3] = a[4] = a[5] = a[6] = a[7] = a[8] = 1;
	a[9] = 2;
	a[10] = 3;
	a[11] = 4;
	a[12] = 5;
	a[13] = 6;
	a[14] = 7;
	a[15] = 8;
	a[16] = 9;
	a[17] = 10;
	a[18] = 14;
	a[19] = 17;
	a[20] = 21;
	a[21] = 25;
	a[22] = 30;
	a[23] = 35;
	a[24] = 45;
	a[25] = 60;
	a[26] = 80;
	a[27] = 100;
	a[28] = 200;
	a[29] = 300;
	a[30] = 400;
	a[31] = 500;

	b[0] = b[1] = b[2] = b[3] = b[4] = b[5] = b[6] = b[7] = b[8] = 0;
	b[9] = b[10] = 1;
	b[11] = b[12] = 2;
	b[13] = b[14] = b[15] = b[16] = 3;
	b[17] = b[18] = b[19] = b[20] = b[21] = 4;
	b[22] = b[23] = b[24] = b[25] = b[26] = b[27] = 5;
	b[28] = b[29] = 6;
	b[30] = b[31] = 7;
}

/* The minimum additive increment value for the congestion control table */
#define CC_MIN_INCR 2U

/**
 *	t4_load_mtus - write the MTU and congestion control HW tables
 *	@adap: the adapter
 *	@mtus: the values for the MTU table
 *	@alpha: the values for the congestion control alpha parameter
 *	@beta: the values for the congestion control beta parameter
 *
 *	Write the HW MTU table with the supplied MTUs and the high-speed
 *	congestion control table with the supplied alpha, beta, and MTUs.
 *	We write the two tables together because the additive increments
 *	depend on the MTUs.
 */
void t4_load_mtus(struct adapter *adap, const unsigned short *mtus,
		  const unsigned short *alpha, const unsigned short *beta)
{
	static const unsigned int avg_pkts[NCCTRL_WIN] = {
		2, 6, 10, 14, 20, 28, 40, 56, 80, 112, 160, 224, 320, 448, 640,
		896, 1281, 1792, 2560, 3584, 5120, 7168, 10240, 14336, 20480,
		28672, 40960, 57344, 81920, 114688, 163840, 229376
	};

	unsigned int i, w;

	for (i = 0; i < NMTUS; ++i) {
		unsigned int mtu = mtus[i];
		unsigned int log2 = fls(mtu);

		if (!(mtu & ((1 << log2) >> 2)))     /* round */
			log2--;
		t4_write_reg(adap, TP_MTU_TABLE_A, MTUINDEX_V(i) |
			     MTUWIDTH_V(log2) | MTUVALUE_V(mtu));

		for (w = 0; w < NCCTRL_WIN; ++w) {
			unsigned int inc;

			inc = max(((mtu - 40) * alpha[w]) / avg_pkts[w],
				  CC_MIN_INCR);

			t4_write_reg(adap, TP_CCTRL_TABLE_A, (i << 21) |
				     (w << 16) | (beta[w] << 13) | inc);
		}
	}
}

/* Calculates a rate in bytes/s given the number of 256-byte units per 4K core
 * clocks.  The formula is
 *
 * bytes/s = bytes256 * 256 * ClkFreq / 4096
 *
 * which is equivalent to
 *
 * bytes/s = 62.5 * bytes256 * ClkFreq_ms
 */
static u64 chan_rate(struct adapter *adap, unsigned int bytes256)
{
	u64 v = bytes256 * adap->params.vpd.cclk;

	return v * 62 + v / 2;
}

/**
 *	t4_get_chan_txrate - get the current per channel Tx rates
 *	@adap: the adapter
 *	@nic_rate: rates for NIC traffic
 *	@ofld_rate: rates for offloaded traffic
 *
 *	Return the current Tx rates in bytes/s for NIC and offloaded traffic
 *	for each channel.
 */
void t4_get_chan_txrate(struct adapter *adap, u64 *nic_rate, u64 *ofld_rate)
{
	u32 v;

	v = t4_read_reg(adap, TP_TX_TRATE_A);
	nic_rate[0] = chan_rate(adap, TNLRATE0_G(v));
	nic_rate[1] = chan_rate(adap, TNLRATE1_G(v));
	if (adap->params.arch.nchan == NCHAN) {
		nic_rate[2] = chan_rate(adap, TNLRATE2_G(v));
		nic_rate[3] = chan_rate(adap, TNLRATE3_G(v));
	}

	v = t4_read_reg(adap, TP_TX_ORATE_A);
	ofld_rate[0] = chan_rate(adap, OFDRATE0_G(v));
	ofld_rate[1] = chan_rate(adap, OFDRATE1_G(v));
	if (adap->params.arch.nchan == NCHAN) {
		ofld_rate[2] = chan_rate(adap, OFDRATE2_G(v));
		ofld_rate[3] = chan_rate(adap, OFDRATE3_G(v));
	}
}

/**
 *	t4_pmtx_get_stats - returns the HW stats from PMTX
 *	@adap: the adapter
 *	@cnt: where to store the count statistics
 *	@cycles: where to store the cycle statistics
 *
 *	Returns performance statistics from PMTX.
 */
void t4_pmtx_get_stats(struct adapter *adap, u32 cnt[], u64 cycles[])
{
	int i;
	u32 data[2];

	for (i = 0; i < PM_NSTATS; i++) {
		t4_write_reg(adap, PM_TX_STAT_CONFIG_A, i + 1);
		cnt[i] = t4_read_reg(adap, PM_TX_STAT_COUNT_A);
		if (is_t4(adap->params.chip)) {
			cycles[i] = t4_read_reg64(adap, PM_TX_STAT_LSB_A);
		} else {
			t4_read_indirect(adap, PM_TX_DBG_CTRL_A,
					 PM_TX_DBG_DATA_A, data, 2,
					 PM_TX_DBG_STAT_MSB_A);
			cycles[i] = (((u64)data[0] << 32) | data[1]);
		}
	}
}

/**
 *	t4_pmrx_get_stats - returns the HW stats from PMRX
 *	@adap: the adapter
 *	@cnt: where to store the count statistics
 *	@cycles: where to store the cycle statistics
 *
 *	Returns performance statistics from PMRX.
 */
void t4_pmrx_get_stats(struct adapter *adap, u32 cnt[], u64 cycles[])
{
	int i;
	u32 data[2];

	for (i = 0; i < PM_NSTATS; i++) {
		t4_write_reg(adap, PM_RX_STAT_CONFIG_A, i + 1);
		cnt[i] = t4_read_reg(adap, PM_RX_STAT_COUNT_A);
		if (is_t4(adap->params.chip)) {
			cycles[i] = t4_read_reg64(adap, PM_RX_STAT_LSB_A);
		} else {
			t4_read_indirect(adap, PM_RX_DBG_CTRL_A,
					 PM_RX_DBG_DATA_A, data, 2,
					 PM_RX_DBG_STAT_MSB_A);
			cycles[i] = (((u64)data[0] << 32) | data[1]);
		}
	}
}

/**
 *	t4_get_mps_bg_map - return the buffer groups associated with a port
 *	@adap: the adapter
 *	@idx: the port index
 *
 *	Returns a bitmap indicating which MPS buffer groups are associated
 *	with the given port.  Bit i is set if buffer group i is used by the
 *	port.
 */
unsigned int t4_get_mps_bg_map(struct adapter *adap, int idx)
{
	u32 n = NUMPORTS_G(t4_read_reg(adap, MPS_CMN_CTL_A));

	if (n == 0)
		return idx == 0 ? 0xf : 0;
	if (n == 1)
		return idx < 2 ? (3 << (2 * idx)) : 0;
	return 1 << idx;
}

/**
 *      t4_get_port_type_description - return Port Type string description
 *      @port_type: firmware Port Type enumeration
 */
const char *t4_get_port_type_description(enum fw_port_type port_type)
{
	static const char *const port_type_description[] = {
		"R XFI",
		"R XAUI",
		"T SGMII",
		"T XFI",
		"T XAUI",
		"KX4",
		"CX4",
		"KX",
		"KR",
		"R SFP+",
		"KR/KX",
		"KR/KX/KX4",
		"R QSFP_10G",
		"R QSA",
		"R QSFP",
		"R BP40_BA",
	};

	if (port_type < ARRAY_SIZE(port_type_description))
		return port_type_description[port_type];
	return "UNKNOWN";
}

/**
 *      t4_get_port_stats_offset - collect port stats relative to a previous
 *                                 snapshot
 *      @adap: The adapter
 *      @idx: The port
 *      @stats: Current stats to fill
 *      @offset: Previous stats snapshot
 */
void t4_get_port_stats_offset(struct adapter *adap, int idx,
			      struct port_stats *stats,
			      struct port_stats *offset)
{
	u64 *s, *o;
	int i;

	t4_get_port_stats(adap, idx, stats);
	for (i = 0, s = (u64 *)stats, o = (u64 *)offset;
			i < (sizeof(struct port_stats) / sizeof(u64));
			i++, s++, o++)
		*s -= *o;
}

/**
 *	t4_get_port_stats - collect port statistics
 *	@adap: the adapter
 *	@idx: the port index
 *	@p: the stats structure to fill
 *
 *	Collect statistics related to the given port from HW.
 */
void t4_get_port_stats(struct adapter *adap, int idx, struct port_stats *p)
{
	u32 bgmap = t4_get_mps_bg_map(adap, idx);

#define GET_STAT(name) \
	t4_read_reg64(adap, \
	(is_t4(adap->params.chip) ? PORT_REG(idx, MPS_PORT_STAT_##name##_L) : \
	T5_PORT_REG(idx, MPS_PORT_STAT_##name##_L)))
#define GET_STAT_COM(name) t4_read_reg64(adap, MPS_STAT_##name##_L)

	p->tx_octets           = GET_STAT(TX_PORT_BYTES);
	p->tx_frames           = GET_STAT(TX_PORT_FRAMES);
	p->tx_bcast_frames     = GET_STAT(TX_PORT_BCAST);
	p->tx_mcast_frames     = GET_STAT(TX_PORT_MCAST);
	p->tx_ucast_frames     = GET_STAT(TX_PORT_UCAST);
	p->tx_error_frames     = GET_STAT(TX_PORT_ERROR);
	p->tx_frames_64        = GET_STAT(TX_PORT_64B);
	p->tx_frames_65_127    = GET_STAT(TX_PORT_65B_127B);
	p->tx_frames_128_255   = GET_STAT(TX_PORT_128B_255B);
	p->tx_frames_256_511   = GET_STAT(TX_PORT_256B_511B);
	p->tx_frames_512_1023  = GET_STAT(TX_PORT_512B_1023B);
	p->tx_frames_1024_1518 = GET_STAT(TX_PORT_1024B_1518B);
	p->tx_frames_1519_max  = GET_STAT(TX_PORT_1519B_MAX);
	p->tx_drop             = GET_STAT(TX_PORT_DROP);
	p->tx_pause            = GET_STAT(TX_PORT_PAUSE);
	p->tx_ppp0             = GET_STAT(TX_PORT_PPP0);
	p->tx_ppp1             = GET_STAT(TX_PORT_PPP1);
	p->tx_ppp2             = GET_STAT(TX_PORT_PPP2);
	p->tx_ppp3             = GET_STAT(TX_PORT_PPP3);
	p->tx_ppp4             = GET_STAT(TX_PORT_PPP4);
	p->tx_ppp5             = GET_STAT(TX_PORT_PPP5);
	p->tx_ppp6             = GET_STAT(TX_PORT_PPP6);
	p->tx_ppp7             = GET_STAT(TX_PORT_PPP7);

	p->rx_octets           = GET_STAT(RX_PORT_BYTES);
	p->rx_frames           = GET_STAT(RX_PORT_FRAMES);
	p->rx_bcast_frames     = GET_STAT(RX_PORT_BCAST);
	p->rx_mcast_frames     = GET_STAT(RX_PORT_MCAST);
	p->rx_ucast_frames     = GET_STAT(RX_PORT_UCAST);
	p->rx_too_long         = GET_STAT(RX_PORT_MTU_ERROR);
	p->rx_jabber           = GET_STAT(RX_PORT_MTU_CRC_ERROR);
	p->rx_fcs_err          = GET_STAT(RX_PORT_CRC_ERROR);
	p->rx_len_err          = GET_STAT(RX_PORT_LEN_ERROR);
	p->rx_symbol_err       = GET_STAT(RX_PORT_SYM_ERROR);
	p->rx_runt             = GET_STAT(RX_PORT_LESS_64B);
	p->rx_frames_64        = GET_STAT(RX_PORT_64B);
	p->rx_frames_65_127    = GET_STAT(RX_PORT_65B_127B);
	p->rx_frames_128_255   = GET_STAT(RX_PORT_128B_255B);
	p->rx_frames_256_511   = GET_STAT(RX_PORT_256B_511B);
	p->rx_frames_512_1023  = GET_STAT(RX_PORT_512B_1023B);
	p->rx_frames_1024_1518 = GET_STAT(RX_PORT_1024B_1518B);
	p->rx_frames_1519_max  = GET_STAT(RX_PORT_1519B_MAX);
	p->rx_pause            = GET_STAT(RX_PORT_PAUSE);
	p->rx_ppp0             = GET_STAT(RX_PORT_PPP0);
	p->rx_ppp1             = GET_STAT(RX_PORT_PPP1);
	p->rx_ppp2             = GET_STAT(RX_PORT_PPP2);
	p->rx_ppp3             = GET_STAT(RX_PORT_PPP3);
	p->rx_ppp4             = GET_STAT(RX_PORT_PPP4);
	p->rx_ppp5             = GET_STAT(RX_PORT_PPP5);
	p->rx_ppp6             = GET_STAT(RX_PORT_PPP6);
	p->rx_ppp7             = GET_STAT(RX_PORT_PPP7);

	p->rx_ovflow0 = (bgmap & 1) ? GET_STAT_COM(RX_BG_0_MAC_DROP_FRAME) : 0;
	p->rx_ovflow1 = (bgmap & 2) ? GET_STAT_COM(RX_BG_1_MAC_DROP_FRAME) : 0;
	p->rx_ovflow2 = (bgmap & 4) ? GET_STAT_COM(RX_BG_2_MAC_DROP_FRAME) : 0;
	p->rx_ovflow3 = (bgmap & 8) ? GET_STAT_COM(RX_BG_3_MAC_DROP_FRAME) : 0;
	p->rx_trunc0 = (bgmap & 1) ? GET_STAT_COM(RX_BG_0_MAC_TRUNC_FRAME) : 0;
	p->rx_trunc1 = (bgmap & 2) ? GET_STAT_COM(RX_BG_1_MAC_TRUNC_FRAME) : 0;
	p->rx_trunc2 = (bgmap & 4) ? GET_STAT_COM(RX_BG_2_MAC_TRUNC_FRAME) : 0;
	p->rx_trunc3 = (bgmap & 8) ? GET_STAT_COM(RX_BG_3_MAC_TRUNC_FRAME) : 0;

#undef GET_STAT
#undef GET_STAT_COM
}

/**
 *	t4_get_lb_stats - collect loopback port statistics
 *	@adap: the adapter
 *	@idx: the loopback port index
 *	@p: the stats structure to fill
 *
 *	Return HW statistics for the given loopback port.
 */
void t4_get_lb_stats(struct adapter *adap, int idx, struct lb_port_stats *p)
{
	u32 bgmap = t4_get_mps_bg_map(adap, idx);

#define GET_STAT(name) \
	t4_read_reg64(adap, \
	(is_t4(adap->params.chip) ? \
	PORT_REG(idx, MPS_PORT_STAT_LB_PORT_##name##_L) : \
	T5_PORT_REG(idx, MPS_PORT_STAT_LB_PORT_##name##_L)))
#define GET_STAT_COM(name) t4_read_reg64(adap, MPS_STAT_##name##_L)

	p->octets           = GET_STAT(BYTES);
	p->frames           = GET_STAT(FRAMES);
	p->bcast_frames     = GET_STAT(BCAST);
	p->mcast_frames     = GET_STAT(MCAST);
	p->ucast_frames     = GET_STAT(UCAST);
	p->error_frames     = GET_STAT(ERROR);

	p->frames_64        = GET_STAT(64B);
	p->frames_65_127    = GET_STAT(65B_127B);
	p->frames_128_255   = GET_STAT(128B_255B);
	p->frames_256_511   = GET_STAT(256B_511B);
	p->frames_512_1023  = GET_STAT(512B_1023B);
	p->frames_1024_1518 = GET_STAT(1024B_1518B);
	p->frames_1519_max  = GET_STAT(1519B_MAX);
	p->drop             = GET_STAT(DROP_FRAMES);

	p->ovflow0 = (bgmap & 1) ? GET_STAT_COM(RX_BG_0_LB_DROP_FRAME) : 0;
	p->ovflow1 = (bgmap & 2) ? GET_STAT_COM(RX_BG_1_LB_DROP_FRAME) : 0;
	p->ovflow2 = (bgmap & 4) ? GET_STAT_COM(RX_BG_2_LB_DROP_FRAME) : 0;
	p->ovflow3 = (bgmap & 8) ? GET_STAT_COM(RX_BG_3_LB_DROP_FRAME) : 0;
	p->trunc0 = (bgmap & 1) ? GET_STAT_COM(RX_BG_0_LB_TRUNC_FRAME) : 0;
	p->trunc1 = (bgmap & 2) ? GET_STAT_COM(RX_BG_1_LB_TRUNC_FRAME) : 0;
	p->trunc2 = (bgmap & 4) ? GET_STAT_COM(RX_BG_2_LB_TRUNC_FRAME) : 0;
	p->trunc3 = (bgmap & 8) ? GET_STAT_COM(RX_BG_3_LB_TRUNC_FRAME) : 0;

#undef GET_STAT
#undef GET_STAT_COM
}

/*     t4_mk_filtdelwr - create a delete filter WR
 *     @ftid: the filter ID
 *     @wr: the filter work request to populate
 *     @qid: ingress queue to receive the delete notification
 *
 *     Creates a filter work request to delete the supplied filter.  If @qid is
 *     negative the delete notification is suppressed.
 */
void t4_mk_filtdelwr(unsigned int ftid, struct fw_filter_wr *wr, int qid)
{
	memset(wr, 0, sizeof(*wr));
	wr->op_pkd = cpu_to_be32(FW_WR_OP_V(FW_FILTER_WR));
	wr->len16_pkd = cpu_to_be32(FW_WR_LEN16_V(sizeof(*wr) / 16));
	wr->tid_to_iq = cpu_to_be32(FW_FILTER_WR_TID_V(ftid) |
				    FW_FILTER_WR_NOREPLY_V(qid < 0));
	wr->del_filter_to_l2tix = cpu_to_be32(FW_FILTER_WR_DEL_FILTER_F);
	if (qid >= 0)
		wr->rx_chan_rx_rpl_iq =
			cpu_to_be16(FW_FILTER_WR_RX_RPL_IQ_V(qid));
}

#define INIT_CMD(var, cmd, rd_wr) do { \
	(var).op_to_write = cpu_to_be32(FW_CMD_OP_V(FW_##cmd##_CMD) | \
					FW_CMD_REQUEST_F | \
					FW_CMD_##rd_wr##_F); \
	(var).retval_len16 = cpu_to_be32(FW_LEN16(var)); \
} while (0)

int t4_fwaddrspace_write(struct adapter *adap, unsigned int mbox,
			  u32 addr, u32 val)
{
	u32 ldst_addrspace;
	struct fw_ldst_cmd c;

	memset(&c, 0, sizeof(c));
	ldst_addrspace = FW_LDST_CMD_ADDRSPACE_V(FW_LDST_ADDRSPC_FIRMWARE);
	c.op_to_addrspace = cpu_to_be32(FW_CMD_OP_V(FW_LDST_CMD) |
					FW_CMD_REQUEST_F |
					FW_CMD_WRITE_F |
					ldst_addrspace);
	c.cycles_to_len16 = cpu_to_be32(FW_LEN16(c));
	c.u.addrval.addr = cpu_to_be32(addr);
	c.u.addrval.val = cpu_to_be32(val);

	return t4_wr_mbox(adap, mbox, &c, sizeof(c), NULL);
}

/**
 *	t4_mdio_rd - read a PHY register through MDIO
 *	@adap: the adapter
 *	@mbox: mailbox to use for the FW command
 *	@phy_addr: the PHY address
 *	@mmd: the PHY MMD to access (0 for clause 22 PHYs)
 *	@reg: the register to read
 *	@valp: where to store the value
 *
 *	Issues a FW command through the given mailbox to read a PHY register.
 */
int t4_mdio_rd(struct adapter *adap, unsigned int mbox, unsigned int phy_addr,
	       unsigned int mmd, unsigned int reg, u16 *valp)
{
	int ret;
	u32 ldst_addrspace;
	struct fw_ldst_cmd c;

	memset(&c, 0, sizeof(c));
	ldst_addrspace = FW_LDST_CMD_ADDRSPACE_V(FW_LDST_ADDRSPC_MDIO);
	c.op_to_addrspace = cpu_to_be32(FW_CMD_OP_V(FW_LDST_CMD) |
					FW_CMD_REQUEST_F | FW_CMD_READ_F |
					ldst_addrspace);
	c.cycles_to_len16 = cpu_to_be32(FW_LEN16(c));
	c.u.mdio.paddr_mmd = cpu_to_be16(FW_LDST_CMD_PADDR_V(phy_addr) |
					 FW_LDST_CMD_MMD_V(mmd));
	c.u.mdio.raddr = cpu_to_be16(reg);

	ret = t4_wr_mbox(adap, mbox, &c, sizeof(c), &c);
	if (ret == 0)
		*valp = be16_to_cpu(c.u.mdio.rval);
	return ret;
}

/**
 *	t4_mdio_wr - write a PHY register through MDIO
 *	@adap: the adapter
 *	@mbox: mailbox to use for the FW command
 *	@phy_addr: the PHY address
 *	@mmd: the PHY MMD to access (0 for clause 22 PHYs)
 *	@reg: the register to write
 *	@valp: value to write
 *
 *	Issues a FW command through the given mailbox to write a PHY register.
 */
int t4_mdio_wr(struct adapter *adap, unsigned int mbox, unsigned int phy_addr,
	       unsigned int mmd, unsigned int reg, u16 val)
{
	u32 ldst_addrspace;
	struct fw_ldst_cmd c;

	memset(&c, 0, sizeof(c));
	ldst_addrspace = FW_LDST_CMD_ADDRSPACE_V(FW_LDST_ADDRSPC_MDIO);
	c.op_to_addrspace = cpu_to_be32(FW_CMD_OP_V(FW_LDST_CMD) |
					FW_CMD_REQUEST_F | FW_CMD_WRITE_F |
					ldst_addrspace);
	c.cycles_to_len16 = cpu_to_be32(FW_LEN16(c));
	c.u.mdio.paddr_mmd = cpu_to_be16(FW_LDST_CMD_PADDR_V(phy_addr) |
					 FW_LDST_CMD_MMD_V(mmd));
	c.u.mdio.raddr = cpu_to_be16(reg);
	c.u.mdio.rval = cpu_to_be16(val);

	return t4_wr_mbox(adap, mbox, &c, sizeof(c), NULL);
}

/**
 *	t4_sge_decode_idma_state - decode the idma state
 *	@adap: the adapter
 *	@state: the state idma is stuck in
 */
void t4_sge_decode_idma_state(struct adapter *adapter, int state)
{
	static const char * const t4_decode[] = {
		"IDMA_IDLE",
		"IDMA_PUSH_MORE_CPL_FIFO",
		"IDMA_PUSH_CPL_MSG_HEADER_TO_FIFO",
		"Not used",
		"IDMA_PHYSADDR_SEND_PCIEHDR",
		"IDMA_PHYSADDR_SEND_PAYLOAD_FIRST",
		"IDMA_PHYSADDR_SEND_PAYLOAD",
		"IDMA_SEND_FIFO_TO_IMSG",
		"IDMA_FL_REQ_DATA_FL_PREP",
		"IDMA_FL_REQ_DATA_FL",
		"IDMA_FL_DROP",
		"IDMA_FL_H_REQ_HEADER_FL",
		"IDMA_FL_H_SEND_PCIEHDR",
		"IDMA_FL_H_PUSH_CPL_FIFO",
		"IDMA_FL_H_SEND_CPL",
		"IDMA_FL_H_SEND_IP_HDR_FIRST",
		"IDMA_FL_H_SEND_IP_HDR",
		"IDMA_FL_H_REQ_NEXT_HEADER_FL",
		"IDMA_FL_H_SEND_NEXT_PCIEHDR",
		"IDMA_FL_H_SEND_IP_HDR_PADDING",
		"IDMA_FL_D_SEND_PCIEHDR",
		"IDMA_FL_D_SEND_CPL_AND_IP_HDR",
		"IDMA_FL_D_REQ_NEXT_DATA_FL",
		"IDMA_FL_SEND_PCIEHDR",
		"IDMA_FL_PUSH_CPL_FIFO",
		"IDMA_FL_SEND_CPL",
		"IDMA_FL_SEND_PAYLOAD_FIRST",
		"IDMA_FL_SEND_PAYLOAD",
		"IDMA_FL_REQ_NEXT_DATA_FL",
		"IDMA_FL_SEND_NEXT_PCIEHDR",
		"IDMA_FL_SEND_PADDING",
		"IDMA_FL_SEND_COMPLETION_TO_IMSG",
		"IDMA_FL_SEND_FIFO_TO_IMSG",
		"IDMA_FL_REQ_DATAFL_DONE",
		"IDMA_FL_REQ_HEADERFL_DONE",
	};
	static const char * const t5_decode[] = {
		"IDMA_IDLE",
		"IDMA_ALMOST_IDLE",
		"IDMA_PUSH_MORE_CPL_FIFO",
		"IDMA_PUSH_CPL_MSG_HEADER_TO_FIFO",
		"IDMA_SGEFLRFLUSH_SEND_PCIEHDR",
		"IDMA_PHYSADDR_SEND_PCIEHDR",
		"IDMA_PHYSADDR_SEND_PAYLOAD_FIRST",
		"IDMA_PHYSADDR_SEND_PAYLOAD",
		"IDMA_SEND_FIFO_TO_IMSG",
		"IDMA_FL_REQ_DATA_FL",
		"IDMA_FL_DROP",
		"IDMA_FL_DROP_SEND_INC",
		"IDMA_FL_H_REQ_HEADER_FL",
		"IDMA_FL_H_SEND_PCIEHDR",
		"IDMA_FL_H_PUSH_CPL_FIFO",
		"IDMA_FL_H_SEND_CPL",
		"IDMA_FL_H_SEND_IP_HDR_FIRST",
		"IDMA_FL_H_SEND_IP_HDR",
		"IDMA_FL_H_REQ_NEXT_HEADER_FL",
		"IDMA_FL_H_SEND_NEXT_PCIEHDR",
		"IDMA_FL_H_SEND_IP_HDR_PADDING",
		"IDMA_FL_D_SEND_PCIEHDR",
		"IDMA_FL_D_SEND_CPL_AND_IP_HDR",
		"IDMA_FL_D_REQ_NEXT_DATA_FL",
		"IDMA_FL_SEND_PCIEHDR",
		"IDMA_FL_PUSH_CPL_FIFO",
		"IDMA_FL_SEND_CPL",
		"IDMA_FL_SEND_PAYLOAD_FIRST",
		"IDMA_FL_SEND_PAYLOAD",
		"IDMA_FL_REQ_NEXT_DATA_FL",
		"IDMA_FL_SEND_NEXT_PCIEHDR",
		"IDMA_FL_SEND_PADDING",
		"IDMA_FL_SEND_COMPLETION_TO_IMSG",
	};
	static const u32 sge_regs[] = {
		SGE_DEBUG_DATA_LOW_INDEX_2_A,
		SGE_DEBUG_DATA_LOW_INDEX_3_A,
		SGE_DEBUG_DATA_HIGH_INDEX_10_A,
	};
	const char **sge_idma_decode;
	int sge_idma_decode_nstates;
	int i;

	if (is_t4(adapter->params.chip)) {
		sge_idma_decode = (const char **)t4_decode;
		sge_idma_decode_nstates = ARRAY_SIZE(t4_decode);
	} else {
		sge_idma_decode = (const char **)t5_decode;
		sge_idma_decode_nstates = ARRAY_SIZE(t5_decode);
	}

	if (state < sge_idma_decode_nstates)
		CH_WARN(adapter, "idma state %s\n", sge_idma_decode[state]);
	else
		CH_WARN(adapter, "idma state %d unknown\n", state);

	for (i = 0; i < ARRAY_SIZE(sge_regs); i++)
		CH_WARN(adapter, "SGE register %#x value %#x\n",
			sge_regs[i], t4_read_reg(adapter, sge_regs[i]));
}

/**
 *      t4_sge_ctxt_flush - flush the SGE context cache
 *      @adap: the adapter
 *      @mbox: mailbox to use for the FW command
 *
 *      Issues a FW command through the given mailbox to flush the
 *      SGE context cache.
 */
int t4_sge_ctxt_flush(struct adapter *adap, unsigned int mbox)
{
	int ret;
	u32 ldst_addrspace;
	struct fw_ldst_cmd c;

	memset(&c, 0, sizeof(c));
	ldst_addrspace = FW_LDST_CMD_ADDRSPACE_V(FW_LDST_ADDRSPC_SGE_EGRC);
	c.op_to_addrspace = cpu_to_be32(FW_CMD_OP_V(FW_LDST_CMD) |
					FW_CMD_REQUEST_F | FW_CMD_READ_F |
					ldst_addrspace);
	c.cycles_to_len16 = cpu_to_be32(FW_LEN16(c));
	c.u.idctxt.msg_ctxtflush = cpu_to_be32(FW_LDST_CMD_CTXTFLUSH_F);

	ret = t4_wr_mbox(adap, mbox, &c, sizeof(c), &c);
	return ret;
}

/**
 *      t4_fw_hello - establish communication with FW
 *      @adap: the adapter
 *      @mbox: mailbox to use for the FW command
 *      @evt_mbox: mailbox to receive async FW events
 *      @master: specifies the caller's willingness to be the device master
 *	@state: returns the current device state (if non-NULL)
 *
 *	Issues a command to establish communication with FW.  Returns either
 *	an error (negative integer) or the mailbox of the Master PF.
 */
int t4_fw_hello(struct adapter *adap, unsigned int mbox, unsigned int evt_mbox,
		enum dev_master master, enum dev_state *state)
{
	int ret;
	struct fw_hello_cmd c;
	u32 v;
	unsigned int master_mbox;
	int retries = FW_CMD_HELLO_RETRIES;

retry:
	memset(&c, 0, sizeof(c));
	INIT_CMD(c, HELLO, WRITE);
	c.err_to_clearinit = cpu_to_be32(
		FW_HELLO_CMD_MASTERDIS_V(master == MASTER_CANT) |
		FW_HELLO_CMD_MASTERFORCE_V(master == MASTER_MUST) |
		FW_HELLO_CMD_MBMASTER_V(master == MASTER_MUST ?
					mbox : FW_HELLO_CMD_MBMASTER_M) |
		FW_HELLO_CMD_MBASYNCNOT_V(evt_mbox) |
		FW_HELLO_CMD_STAGE_V(fw_hello_cmd_stage_os) |
		FW_HELLO_CMD_CLEARINIT_F);

	/*
	 * Issue the HELLO command to the firmware.  If it's not successful
	 * but indicates that we got a "busy" or "timeout" condition, retry
	 * the HELLO until we exhaust our retry limit.  If we do exceed our
	 * retry limit, check to see if the firmware left us any error
	 * information and report that if so.
	 */
	ret = t4_wr_mbox(adap, mbox, &c, sizeof(c), &c);
	if (ret < 0) {
		if ((ret == -EBUSY || ret == -ETIMEDOUT) && retries-- > 0)
			goto retry;
		if (t4_read_reg(adap, PCIE_FW_A) & PCIE_FW_ERR_F)
			t4_report_fw_error(adap);
		return ret;
	}

	v = be32_to_cpu(c.err_to_clearinit);
	master_mbox = FW_HELLO_CMD_MBMASTER_G(v);
	if (state) {
		if (v & FW_HELLO_CMD_ERR_F)
			*state = DEV_STATE_ERR;
		else if (v & FW_HELLO_CMD_INIT_F)
			*state = DEV_STATE_INIT;
		else
			*state = DEV_STATE_UNINIT;
	}

	/*
	 * If we're not the Master PF then we need to wait around for the
	 * Master PF Driver to finish setting up the adapter.
	 *
	 * Note that we also do this wait if we're a non-Master-capable PF and
	 * there is no current Master PF; a Master PF may show up momentarily
	 * and we wouldn't want to fail pointlessly.  (This can happen when an
	 * OS loads lots of different drivers rapidly at the same time).  In
	 * this case, the Master PF returned by the firmware will be
	 * PCIE_FW_MASTER_M so the test below will work ...
	 */
	if ((v & (FW_HELLO_CMD_ERR_F|FW_HELLO_CMD_INIT_F)) == 0 &&
	    master_mbox != mbox) {
		int waiting = FW_CMD_HELLO_TIMEOUT;

		/*
		 * Wait for the firmware to either indicate an error or
		 * initialized state.  If we see either of these we bail out
		 * and report the issue to the caller.  If we exhaust the
		 * "hello timeout" and we haven't exhausted our retries, try
		 * again.  Otherwise bail with a timeout error.
		 */
		for (;;) {
			u32 pcie_fw;

			msleep(50);
			waiting -= 50;

			/*
			 * If neither Error nor Initialialized are indicated
			 * by the firmware keep waiting till we exaust our
			 * timeout ... and then retry if we haven't exhausted
			 * our retries ...
			 */
			pcie_fw = t4_read_reg(adap, PCIE_FW_A);
			if (!(pcie_fw & (PCIE_FW_ERR_F|PCIE_FW_INIT_F))) {
				if (waiting <= 0) {
					if (retries-- > 0)
						goto retry;

					return -ETIMEDOUT;
				}
				continue;
			}

			/*
			 * We either have an Error or Initialized condition
			 * report errors preferentially.
			 */
			if (state) {
				if (pcie_fw & PCIE_FW_ERR_F)
					*state = DEV_STATE_ERR;
				else if (pcie_fw & PCIE_FW_INIT_F)
					*state = DEV_STATE_INIT;
			}

			/*
			 * If we arrived before a Master PF was selected and
			 * there's not a valid Master PF, grab its identity
			 * for our caller.
			 */
			if (master_mbox == PCIE_FW_MASTER_M &&
			    (pcie_fw & PCIE_FW_MASTER_VLD_F))
				master_mbox = PCIE_FW_MASTER_G(pcie_fw);
			break;
		}
	}

	return master_mbox;
}

/**
 *	t4_fw_bye - end communication with FW
 *	@adap: the adapter
 *	@mbox: mailbox to use for the FW command
 *
 *	Issues a command to terminate communication with FW.
 */
int t4_fw_bye(struct adapter *adap, unsigned int mbox)
{
	struct fw_bye_cmd c;

	memset(&c, 0, sizeof(c));
	INIT_CMD(c, BYE, WRITE);
	return t4_wr_mbox(adap, mbox, &c, sizeof(c), NULL);
}

/**
 *	t4_init_cmd - ask FW to initialize the device
 *	@adap: the adapter
 *	@mbox: mailbox to use for the FW command
 *
 *	Issues a command to FW to partially initialize the device.  This
 *	performs initialization that generally doesn't depend on user input.
 */
int t4_early_init(struct adapter *adap, unsigned int mbox)
{
	struct fw_initialize_cmd c;

	memset(&c, 0, sizeof(c));
	INIT_CMD(c, INITIALIZE, WRITE);
	return t4_wr_mbox(adap, mbox, &c, sizeof(c), NULL);
}

/**
 *	t4_fw_reset - issue a reset to FW
 *	@adap: the adapter
 *	@mbox: mailbox to use for the FW command
 *	@reset: specifies the type of reset to perform
 *
 *	Issues a reset command of the specified type to FW.
 */
int t4_fw_reset(struct adapter *adap, unsigned int mbox, int reset)
{
	struct fw_reset_cmd c;

	memset(&c, 0, sizeof(c));
	INIT_CMD(c, RESET, WRITE);
	c.val = cpu_to_be32(reset);
	return t4_wr_mbox(adap, mbox, &c, sizeof(c), NULL);
}

/**
 *	t4_fw_halt - issue a reset/halt to FW and put uP into RESET
 *	@adap: the adapter
 *	@mbox: mailbox to use for the FW RESET command (if desired)
 *	@force: force uP into RESET even if FW RESET command fails
 *
 *	Issues a RESET command to firmware (if desired) with a HALT indication
 *	and then puts the microprocessor into RESET state.  The RESET command
 *	will only be issued if a legitimate mailbox is provided (mbox <=
 *	PCIE_FW_MASTER_M).
 *
 *	This is generally used in order for the host to safely manipulate the
 *	adapter without fear of conflicting with whatever the firmware might
 *	be doing.  The only way out of this state is to RESTART the firmware
 *	...
 */
static int t4_fw_halt(struct adapter *adap, unsigned int mbox, int force)
{
	int ret = 0;

	/*
	 * If a legitimate mailbox is provided, issue a RESET command
	 * with a HALT indication.
	 */
	if (mbox <= PCIE_FW_MASTER_M) {
		struct fw_reset_cmd c;

		memset(&c, 0, sizeof(c));
		INIT_CMD(c, RESET, WRITE);
		c.val = cpu_to_be32(PIORST_F | PIORSTMODE_F);
		c.halt_pkd = cpu_to_be32(FW_RESET_CMD_HALT_F);
		ret = t4_wr_mbox(adap, mbox, &c, sizeof(c), NULL);
	}

	/*
	 * Normally we won't complete the operation if the firmware RESET
	 * command fails but if our caller insists we'll go ahead and put the
	 * uP into RESET.  This can be useful if the firmware is hung or even
	 * missing ...  We'll have to take the risk of putting the uP into
	 * RESET without the cooperation of firmware in that case.
	 *
	 * We also force the firmware's HALT flag to be on in case we bypassed
	 * the firmware RESET command above or we're dealing with old firmware
	 * which doesn't have the HALT capability.  This will serve as a flag
	 * for the incoming firmware to know that it's coming out of a HALT
	 * rather than a RESET ... if it's new enough to understand that ...
	 */
	if (ret == 0 || force) {
		t4_set_reg_field(adap, CIM_BOOT_CFG_A, UPCRST_F, UPCRST_F);
		t4_set_reg_field(adap, PCIE_FW_A, PCIE_FW_HALT_F,
				 PCIE_FW_HALT_F);
	}

	/*
	 * And we always return the result of the firmware RESET command
	 * even when we force the uP into RESET ...
	 */
	return ret;
}

/**
 *	t4_fw_restart - restart the firmware by taking the uP out of RESET
 *	@adap: the adapter
 *	@reset: if we want to do a RESET to restart things
 *
 *	Restart firmware previously halted by t4_fw_halt().  On successful
 *	return the previous PF Master remains as the new PF Master and there
 *	is no need to issue a new HELLO command, etc.
 *
 *	We do this in two ways:
 *
 *	 1. If we're dealing with newer firmware we'll simply want to take
 *	    the chip's microprocessor out of RESET.  This will cause the
 *	    firmware to start up from its start vector.  And then we'll loop
 *	    until the firmware indicates it's started again (PCIE_FW.HALT
 *	    reset to 0) or we timeout.
 *
 *	 2. If we're dealing with older firmware then we'll need to RESET
 *	    the chip since older firmware won't recognize the PCIE_FW.HALT
 *	    flag and automatically RESET itself on startup.
 */
static int t4_fw_restart(struct adapter *adap, unsigned int mbox, int reset)
{
	if (reset) {
		/*
		 * Since we're directing the RESET instead of the firmware
		 * doing it automatically, we need to clear the PCIE_FW.HALT
		 * bit.
		 */
		t4_set_reg_field(adap, PCIE_FW_A, PCIE_FW_HALT_F, 0);

		/*
		 * If we've been given a valid mailbox, first try to get the
		 * firmware to do the RESET.  If that works, great and we can
		 * return success.  Otherwise, if we haven't been given a
		 * valid mailbox or the RESET command failed, fall back to
		 * hitting the chip with a hammer.
		 */
		if (mbox <= PCIE_FW_MASTER_M) {
			t4_set_reg_field(adap, CIM_BOOT_CFG_A, UPCRST_F, 0);
			msleep(100);
			if (t4_fw_reset(adap, mbox,
					PIORST_F | PIORSTMODE_F) == 0)
				return 0;
		}

		t4_write_reg(adap, PL_RST_A, PIORST_F | PIORSTMODE_F);
		msleep(2000);
	} else {
		int ms;

		t4_set_reg_field(adap, CIM_BOOT_CFG_A, UPCRST_F, 0);
		for (ms = 0; ms < FW_CMD_MAX_TIMEOUT; ) {
			if (!(t4_read_reg(adap, PCIE_FW_A) & PCIE_FW_HALT_F))
				return 0;
			msleep(100);
			ms += 100;
		}
		return -ETIMEDOUT;
	}
	return 0;
}

/**
 *	t4_fw_upgrade - perform all of the steps necessary to upgrade FW
 *	@adap: the adapter
 *	@mbox: mailbox to use for the FW RESET command (if desired)
 *	@fw_data: the firmware image to write
 *	@size: image size
 *	@force: force upgrade even if firmware doesn't cooperate
 *
 *	Perform all of the steps necessary for upgrading an adapter's
 *	firmware image.  Normally this requires the cooperation of the
 *	existing firmware in order to halt all existing activities
 *	but if an invalid mailbox token is passed in we skip that step
 *	(though we'll still put the adapter microprocessor into RESET in
 *	that case).
 *
 *	On successful return the new firmware will have been loaded and
 *	the adapter will have been fully RESET losing all previous setup
 *	state.  On unsuccessful return the adapter may be completely hosed ...
 *	positive errno indicates that the adapter is ~probably~ intact, a
 *	negative errno indicates that things are looking bad ...
 */
int t4_fw_upgrade(struct adapter *adap, unsigned int mbox,
		  const u8 *fw_data, unsigned int size, int force)
{
	const struct fw_hdr *fw_hdr = (const struct fw_hdr *)fw_data;
	int reset, ret;

	if (!t4_fw_matches_chip(adap, fw_hdr))
		return -EINVAL;

	ret = t4_fw_halt(adap, mbox, force);
	if (ret < 0 && !force)
		return ret;

	ret = t4_load_fw(adap, fw_data, size);
	if (ret < 0)
		return ret;

	/*
	 * Older versions of the firmware don't understand the new
	 * PCIE_FW.HALT flag and so won't know to perform a RESET when they
	 * restart.  So for newly loaded older firmware we'll have to do the
	 * RESET for it so it starts up on a clean slate.  We can tell if
	 * the newly loaded firmware will handle this right by checking
	 * its header flags to see if it advertises the capability.
	 */
	reset = ((be32_to_cpu(fw_hdr->flags) & FW_HDR_FLAGS_RESET_HALT) == 0);
	return t4_fw_restart(adap, mbox, reset);
}

/**
 *	t4_fixup_host_params - fix up host-dependent parameters
 *	@adap: the adapter
 *	@page_size: the host's Base Page Size
 *	@cache_line_size: the host's Cache Line Size
 *
 *	Various registers in T4 contain values which are dependent on the
 *	host's Base Page and Cache Line Sizes.  This function will fix all of
 *	those registers with the appropriate values as passed in ...
 */
int t4_fixup_host_params(struct adapter *adap, unsigned int page_size,
			 unsigned int cache_line_size)
{
	unsigned int page_shift = fls(page_size) - 1;
	unsigned int sge_hps = page_shift - 10;
	unsigned int stat_len = cache_line_size > 64 ? 128 : 64;
	unsigned int fl_align = cache_line_size < 32 ? 32 : cache_line_size;
	unsigned int fl_align_log = fls(fl_align) - 1;

	t4_write_reg(adap, SGE_HOST_PAGE_SIZE_A,
		     HOSTPAGESIZEPF0_V(sge_hps) |
		     HOSTPAGESIZEPF1_V(sge_hps) |
		     HOSTPAGESIZEPF2_V(sge_hps) |
		     HOSTPAGESIZEPF3_V(sge_hps) |
		     HOSTPAGESIZEPF4_V(sge_hps) |
		     HOSTPAGESIZEPF5_V(sge_hps) |
		     HOSTPAGESIZEPF6_V(sge_hps) |
		     HOSTPAGESIZEPF7_V(sge_hps));

	if (is_t4(adap->params.chip)) {
		t4_set_reg_field(adap, SGE_CONTROL_A,
				 INGPADBOUNDARY_V(INGPADBOUNDARY_M) |
				 EGRSTATUSPAGESIZE_F,
				 INGPADBOUNDARY_V(fl_align_log -
						  INGPADBOUNDARY_SHIFT_X) |
				 EGRSTATUSPAGESIZE_V(stat_len != 64));
	} else {
		/* T5 introduced the separation of the Free List Padding and
		 * Packing Boundaries.  Thus, we can select a smaller Padding
		 * Boundary to avoid uselessly chewing up PCIe Link and Memory
		 * Bandwidth, and use a Packing Boundary which is large enough
		 * to avoid false sharing between CPUs, etc.
		 *
		 * For the PCI Link, the smaller the Padding Boundary the
		 * better.  For the Memory Controller, a smaller Padding
		 * Boundary is better until we cross under the Memory Line
		 * Size (the minimum unit of transfer to/from Memory).  If we
		 * have a Padding Boundary which is smaller than the Memory
		 * Line Size, that'll involve a Read-Modify-Write cycle on the
		 * Memory Controller which is never good.  For T5 the smallest
		 * Padding Boundary which we can select is 32 bytes which is
		 * larger than any known Memory Controller Line Size so we'll
		 * use that.
		 *
		 * T5 has a different interpretation of the "0" value for the
		 * Packing Boundary.  This corresponds to 16 bytes instead of
		 * the expected 32 bytes.  We never have a Packing Boundary
		 * less than 32 bytes so we can't use that special value but
		 * on the other hand, if we wanted 32 bytes, the best we can
		 * really do is 64 bytes.
		*/
		if (fl_align <= 32) {
			fl_align = 64;
			fl_align_log = 6;
		}
		t4_set_reg_field(adap, SGE_CONTROL_A,
				 INGPADBOUNDARY_V(INGPADBOUNDARY_M) |
				 EGRSTATUSPAGESIZE_F,
				 INGPADBOUNDARY_V(INGPCIEBOUNDARY_32B_X) |
				 EGRSTATUSPAGESIZE_V(stat_len != 64));
		t4_set_reg_field(adap, SGE_CONTROL2_A,
				 INGPACKBOUNDARY_V(INGPACKBOUNDARY_M),
				 INGPACKBOUNDARY_V(fl_align_log -
						   INGPACKBOUNDARY_SHIFT_X));
	}
	/*
	 * Adjust various SGE Free List Host Buffer Sizes.
	 *
	 * This is something of a crock since we're using fixed indices into
	 * the array which are also known by the sge.c code and the T4
	 * Firmware Configuration File.  We need to come up with a much better
	 * approach to managing this array.  For now, the first four entries
	 * are:
	 *
	 *   0: Host Page Size
	 *   1: 64KB
	 *   2: Buffer size corresponding to 1500 byte MTU (unpacked mode)
	 *   3: Buffer size corresponding to 9000 byte MTU (unpacked mode)
	 *
	 * For the single-MTU buffers in unpacked mode we need to include
	 * space for the SGE Control Packet Shift, 14 byte Ethernet header,
	 * possible 4 byte VLAN tag, all rounded up to the next Ingress Packet
	 * Padding boundary.  All of these are accommodated in the Factory
	 * Default Firmware Configuration File but we need to adjust it for
	 * this host's cache line size.
	 */
	t4_write_reg(adap, SGE_FL_BUFFER_SIZE0_A, page_size);
	t4_write_reg(adap, SGE_FL_BUFFER_SIZE2_A,
		     (t4_read_reg(adap, SGE_FL_BUFFER_SIZE2_A) + fl_align-1)
		     & ~(fl_align-1));
	t4_write_reg(adap, SGE_FL_BUFFER_SIZE3_A,
		     (t4_read_reg(adap, SGE_FL_BUFFER_SIZE3_A) + fl_align-1)
		     & ~(fl_align-1));

	t4_write_reg(adap, ULP_RX_TDDP_PSZ_A, HPZ0_V(page_shift - 12));

	return 0;
}

/**
 *	t4_fw_initialize - ask FW to initialize the device
 *	@adap: the adapter
 *	@mbox: mailbox to use for the FW command
 *
 *	Issues a command to FW to partially initialize the device.  This
 *	performs initialization that generally doesn't depend on user input.
 */
int t4_fw_initialize(struct adapter *adap, unsigned int mbox)
{
	struct fw_initialize_cmd c;

	memset(&c, 0, sizeof(c));
	INIT_CMD(c, INITIALIZE, WRITE);
	return t4_wr_mbox(adap, mbox, &c, sizeof(c), NULL);
}

/**
 *	t4_query_params_rw - query FW or device parameters
 *	@adap: the adapter
 *	@mbox: mailbox to use for the FW command
 *	@pf: the PF
 *	@vf: the VF
 *	@nparams: the number of parameters
 *	@params: the parameter names
 *	@val: the parameter values
 *	@rw: Write and read flag
 *
 *	Reads the value of FW or device parameters.  Up to 7 parameters can be
 *	queried at once.
 */
int t4_query_params_rw(struct adapter *adap, unsigned int mbox, unsigned int pf,
		       unsigned int vf, unsigned int nparams, const u32 *params,
		       u32 *val, int rw)
{
	int i, ret;
	struct fw_params_cmd c;
	__be32 *p = &c.param[0].mnem;

	if (nparams > 7)
		return -EINVAL;

	memset(&c, 0, sizeof(c));
	c.op_to_vfn = cpu_to_be32(FW_CMD_OP_V(FW_PARAMS_CMD) |
				  FW_CMD_REQUEST_F | FW_CMD_READ_F |
				  FW_PARAMS_CMD_PFN_V(pf) |
				  FW_PARAMS_CMD_VFN_V(vf));
	c.retval_len16 = cpu_to_be32(FW_LEN16(c));

	for (i = 0; i < nparams; i++) {
		*p++ = cpu_to_be32(*params++);
		if (rw)
			*p = cpu_to_be32(*(val + i));
		p++;
	}

	ret = t4_wr_mbox(adap, mbox, &c, sizeof(c), &c);
	if (ret == 0)
		for (i = 0, p = &c.param[0].val; i < nparams; i++, p += 2)
			*val++ = be32_to_cpu(*p);
	return ret;
}

int t4_query_params(struct adapter *adap, unsigned int mbox, unsigned int pf,
		    unsigned int vf, unsigned int nparams, const u32 *params,
		    u32 *val)
{
	return t4_query_params_rw(adap, mbox, pf, vf, nparams, params, val, 0);
}

/**
 *      t4_set_params_timeout - sets FW or device parameters
 *      @adap: the adapter
 *      @mbox: mailbox to use for the FW command
 *      @pf: the PF
 *      @vf: the VF
 *      @nparams: the number of parameters
 *      @params: the parameter names
 *      @val: the parameter values
 *      @timeout: the timeout time
 *
 *      Sets the value of FW or device parameters.  Up to 7 parameters can be
 *      specified at once.
 */
int t4_set_params_timeout(struct adapter *adap, unsigned int mbox,
			  unsigned int pf, unsigned int vf,
			  unsigned int nparams, const u32 *params,
			  const u32 *val, int timeout)
{
	struct fw_params_cmd c;
	__be32 *p = &c.param[0].mnem;

	if (nparams > 7)
		return -EINVAL;

	memset(&c, 0, sizeof(c));
	c.op_to_vfn = cpu_to_be32(FW_CMD_OP_V(FW_PARAMS_CMD) |
				  FW_CMD_REQUEST_F | FW_CMD_WRITE_F |
				  FW_PARAMS_CMD_PFN_V(pf) |
				  FW_PARAMS_CMD_VFN_V(vf));
	c.retval_len16 = cpu_to_be32(FW_LEN16(c));

	while (nparams--) {
		*p++ = cpu_to_be32(*params++);
		*p++ = cpu_to_be32(*val++);
	}

	return t4_wr_mbox_timeout(adap, mbox, &c, sizeof(c), NULL, timeout);
}

/**
 *	t4_set_params - sets FW or device parameters
 *	@adap: the adapter
 *	@mbox: mailbox to use for the FW command
 *	@pf: the PF
 *	@vf: the VF
 *	@nparams: the number of parameters
 *	@params: the parameter names
 *	@val: the parameter values
 *
 *	Sets the value of FW or device parameters.  Up to 7 parameters can be
 *	specified at once.
 */
int t4_set_params(struct adapter *adap, unsigned int mbox, unsigned int pf,
		  unsigned int vf, unsigned int nparams, const u32 *params,
		  const u32 *val)
{
	return t4_set_params_timeout(adap, mbox, pf, vf, nparams, params, val,
				     FW_CMD_MAX_TIMEOUT);
}

/**
 *	t4_cfg_pfvf - configure PF/VF resource limits
 *	@adap: the adapter
 *	@mbox: mailbox to use for the FW command
 *	@pf: the PF being configured
 *	@vf: the VF being configured
 *	@txq: the max number of egress queues
 *	@txq_eth_ctrl: the max number of egress Ethernet or control queues
 *	@rxqi: the max number of interrupt-capable ingress queues
 *	@rxq: the max number of interruptless ingress queues
 *	@tc: the PCI traffic class
 *	@vi: the max number of virtual interfaces
 *	@cmask: the channel access rights mask for the PF/VF
 *	@pmask: the port access rights mask for the PF/VF
 *	@nexact: the maximum number of exact MPS filters
 *	@rcaps: read capabilities
 *	@wxcaps: write/execute capabilities
 *
 *	Configures resource limits and capabilities for a physical or virtual
 *	function.
 */
int t4_cfg_pfvf(struct adapter *adap, unsigned int mbox, unsigned int pf,
		unsigned int vf, unsigned int txq, unsigned int txq_eth_ctrl,
		unsigned int rxqi, unsigned int rxq, unsigned int tc,
		unsigned int vi, unsigned int cmask, unsigned int pmask,
		unsigned int nexact, unsigned int rcaps, unsigned int wxcaps)
{
	struct fw_pfvf_cmd c;

	memset(&c, 0, sizeof(c));
	c.op_to_vfn = cpu_to_be32(FW_CMD_OP_V(FW_PFVF_CMD) | FW_CMD_REQUEST_F |
				  FW_CMD_WRITE_F | FW_PFVF_CMD_PFN_V(pf) |
				  FW_PFVF_CMD_VFN_V(vf));
	c.retval_len16 = cpu_to_be32(FW_LEN16(c));
	c.niqflint_niq = cpu_to_be32(FW_PFVF_CMD_NIQFLINT_V(rxqi) |
				     FW_PFVF_CMD_NIQ_V(rxq));
	c.type_to_neq = cpu_to_be32(FW_PFVF_CMD_CMASK_V(cmask) |
				    FW_PFVF_CMD_PMASK_V(pmask) |
				    FW_PFVF_CMD_NEQ_V(txq));
	c.tc_to_nexactf = cpu_to_be32(FW_PFVF_CMD_TC_V(tc) |
				      FW_PFVF_CMD_NVI_V(vi) |
				      FW_PFVF_CMD_NEXACTF_V(nexact));
	c.r_caps_to_nethctrl = cpu_to_be32(FW_PFVF_CMD_R_CAPS_V(rcaps) |
					FW_PFVF_CMD_WX_CAPS_V(wxcaps) |
					FW_PFVF_CMD_NETHCTRL_V(txq_eth_ctrl));
	return t4_wr_mbox(adap, mbox, &c, sizeof(c), NULL);
}

/**
 *	t4_alloc_vi - allocate a virtual interface
 *	@adap: the adapter
 *	@mbox: mailbox to use for the FW command
 *	@port: physical port associated with the VI
 *	@pf: the PF owning the VI
 *	@vf: the VF owning the VI
 *	@nmac: number of MAC addresses needed (1 to 5)
 *	@mac: the MAC addresses of the VI
 *	@rss_size: size of RSS table slice associated with this VI
 *
 *	Allocates a virtual interface for the given physical port.  If @mac is
 *	not %NULL it contains the MAC addresses of the VI as assigned by FW.
 *	@mac should be large enough to hold @nmac Ethernet addresses, they are
 *	stored consecutively so the space needed is @nmac * 6 bytes.
 *	Returns a negative error number or the non-negative VI id.
 */
int t4_alloc_vi(struct adapter *adap, unsigned int mbox, unsigned int port,
		unsigned int pf, unsigned int vf, unsigned int nmac, u8 *mac,
		unsigned int *rss_size)
{
	int ret;
	struct fw_vi_cmd c;

	memset(&c, 0, sizeof(c));
	c.op_to_vfn = cpu_to_be32(FW_CMD_OP_V(FW_VI_CMD) | FW_CMD_REQUEST_F |
				  FW_CMD_WRITE_F | FW_CMD_EXEC_F |
				  FW_VI_CMD_PFN_V(pf) | FW_VI_CMD_VFN_V(vf));
	c.alloc_to_len16 = cpu_to_be32(FW_VI_CMD_ALLOC_F | FW_LEN16(c));
	c.portid_pkd = FW_VI_CMD_PORTID_V(port);
	c.nmac = nmac - 1;

	ret = t4_wr_mbox(adap, mbox, &c, sizeof(c), &c);
	if (ret)
		return ret;

	if (mac) {
		memcpy(mac, c.mac, sizeof(c.mac));
		switch (nmac) {
		case 5:
			memcpy(mac + 24, c.nmac3, sizeof(c.nmac3));
		case 4:
			memcpy(mac + 18, c.nmac2, sizeof(c.nmac2));
		case 3:
			memcpy(mac + 12, c.nmac1, sizeof(c.nmac1));
		case 2:
			memcpy(mac + 6,  c.nmac0, sizeof(c.nmac0));
		}
	}
	if (rss_size)
		*rss_size = FW_VI_CMD_RSSSIZE_G(be16_to_cpu(c.rsssize_pkd));
	return FW_VI_CMD_VIID_G(be16_to_cpu(c.type_viid));
}

/**
 *	t4_free_vi - free a virtual interface
 *	@adap: the adapter
 *	@mbox: mailbox to use for the FW command
 *	@pf: the PF owning the VI
 *	@vf: the VF owning the VI
 *	@viid: virtual interface identifiler
 *
 *	Free a previously allocated virtual interface.
 */
int t4_free_vi(struct adapter *adap, unsigned int mbox, unsigned int pf,
	       unsigned int vf, unsigned int viid)
{
	struct fw_vi_cmd c;

	memset(&c, 0, sizeof(c));
	c.op_to_vfn = cpu_to_be32(FW_CMD_OP_V(FW_VI_CMD) |
				  FW_CMD_REQUEST_F |
				  FW_CMD_EXEC_F |
				  FW_VI_CMD_PFN_V(pf) |
				  FW_VI_CMD_VFN_V(vf));
	c.alloc_to_len16 = cpu_to_be32(FW_VI_CMD_FREE_F | FW_LEN16(c));
	c.type_viid = cpu_to_be16(FW_VI_CMD_VIID_V(viid));

	return t4_wr_mbox(adap, mbox, &c, sizeof(c), &c);
}

/**
 *	t4_set_rxmode - set Rx properties of a virtual interface
 *	@adap: the adapter
 *	@mbox: mailbox to use for the FW command
 *	@viid: the VI id
 *	@mtu: the new MTU or -1
 *	@promisc: 1 to enable promiscuous mode, 0 to disable it, -1 no change
 *	@all_multi: 1 to enable all-multi mode, 0 to disable it, -1 no change
 *	@bcast: 1 to enable broadcast Rx, 0 to disable it, -1 no change
 *	@vlanex: 1 to enable HW VLAN extraction, 0 to disable it, -1 no change
 *	@sleep_ok: if true we may sleep while awaiting command completion
 *
 *	Sets Rx properties of a virtual interface.
 */
int t4_set_rxmode(struct adapter *adap, unsigned int mbox, unsigned int viid,
		  int mtu, int promisc, int all_multi, int bcast, int vlanex,
		  bool sleep_ok)
{
	struct fw_vi_rxmode_cmd c;

	/* convert to FW values */
	if (mtu < 0)
		mtu = FW_RXMODE_MTU_NO_CHG;
	if (promisc < 0)
		promisc = FW_VI_RXMODE_CMD_PROMISCEN_M;
	if (all_multi < 0)
		all_multi = FW_VI_RXMODE_CMD_ALLMULTIEN_M;
	if (bcast < 0)
		bcast = FW_VI_RXMODE_CMD_BROADCASTEN_M;
	if (vlanex < 0)
		vlanex = FW_VI_RXMODE_CMD_VLANEXEN_M;

	memset(&c, 0, sizeof(c));
	c.op_to_viid = cpu_to_be32(FW_CMD_OP_V(FW_VI_RXMODE_CMD) |
				   FW_CMD_REQUEST_F | FW_CMD_WRITE_F |
				   FW_VI_RXMODE_CMD_VIID_V(viid));
	c.retval_len16 = cpu_to_be32(FW_LEN16(c));
	c.mtu_to_vlanexen =
		cpu_to_be32(FW_VI_RXMODE_CMD_MTU_V(mtu) |
			    FW_VI_RXMODE_CMD_PROMISCEN_V(promisc) |
			    FW_VI_RXMODE_CMD_ALLMULTIEN_V(all_multi) |
			    FW_VI_RXMODE_CMD_BROADCASTEN_V(bcast) |
			    FW_VI_RXMODE_CMD_VLANEXEN_V(vlanex));
	return t4_wr_mbox_meat(adap, mbox, &c, sizeof(c), NULL, sleep_ok);
}

/**
 *	t4_alloc_mac_filt - allocates exact-match filters for MAC addresses
 *	@adap: the adapter
 *	@mbox: mailbox to use for the FW command
 *	@viid: the VI id
 *	@free: if true any existing filters for this VI id are first removed
 *	@naddr: the number of MAC addresses to allocate filters for (up to 7)
 *	@addr: the MAC address(es)
 *	@idx: where to store the index of each allocated filter
 *	@hash: pointer to hash address filter bitmap
 *	@sleep_ok: call is allowed to sleep
 *
 *	Allocates an exact-match filter for each of the supplied addresses and
 *	sets it to the corresponding address.  If @idx is not %NULL it should
 *	have at least @naddr entries, each of which will be set to the index of
 *	the filter allocated for the corresponding MAC address.  If a filter
 *	could not be allocated for an address its index is set to 0xffff.
 *	If @hash is not %NULL addresses that fail to allocate an exact filter
 *	are hashed and update the hash filter bitmap pointed at by @hash.
 *
 *	Returns a negative error number or the number of filters allocated.
 */
int t4_alloc_mac_filt(struct adapter *adap, unsigned int mbox,
		      unsigned int viid, bool free, unsigned int naddr,
		      const u8 **addr, u16 *idx, u64 *hash, bool sleep_ok)
{
	int offset, ret = 0;
	struct fw_vi_mac_cmd c;
	unsigned int nfilters = 0;
	unsigned int max_naddr = adap->params.arch.mps_tcam_size;
	unsigned int rem = naddr;

	if (naddr > max_naddr)
		return -EINVAL;

	for (offset = 0; offset < naddr ; /**/) {
		unsigned int fw_naddr = (rem < ARRAY_SIZE(c.u.exact) ?
					 rem : ARRAY_SIZE(c.u.exact));
		size_t len16 = DIV_ROUND_UP(offsetof(struct fw_vi_mac_cmd,
						     u.exact[fw_naddr]), 16);
		struct fw_vi_mac_exact *p;
		int i;

		memset(&c, 0, sizeof(c));
		c.op_to_viid = cpu_to_be32(FW_CMD_OP_V(FW_VI_MAC_CMD) |
					   FW_CMD_REQUEST_F |
					   FW_CMD_WRITE_F |
					   FW_CMD_EXEC_V(free) |
					   FW_VI_MAC_CMD_VIID_V(viid));
		c.freemacs_to_len16 =
			cpu_to_be32(FW_VI_MAC_CMD_FREEMACS_V(free) |
				    FW_CMD_LEN16_V(len16));

		for (i = 0, p = c.u.exact; i < fw_naddr; i++, p++) {
			p->valid_to_idx =
				cpu_to_be16(FW_VI_MAC_CMD_VALID_F |
					    FW_VI_MAC_CMD_IDX_V(
						    FW_VI_MAC_ADD_MAC));
			memcpy(p->macaddr, addr[offset + i],
			       sizeof(p->macaddr));
		}

		/* It's okay if we run out of space in our MAC address arena.
		 * Some of the addresses we submit may get stored so we need
		 * to run through the reply to see what the results were ...
		 */
		ret = t4_wr_mbox_meat(adap, mbox, &c, sizeof(c), &c, sleep_ok);
		if (ret && ret != -FW_ENOMEM)
			break;

		for (i = 0, p = c.u.exact; i < fw_naddr; i++, p++) {
			u16 index = FW_VI_MAC_CMD_IDX_G(
					be16_to_cpu(p->valid_to_idx));

			if (idx)
				idx[offset + i] = (index >= max_naddr ?
						   0xffff : index);
			if (index < max_naddr)
				nfilters++;
			else if (hash)
				*hash |= (1ULL <<
					  hash_mac_addr(addr[offset + i]));
		}

		free = false;
		offset += fw_naddr;
		rem -= fw_naddr;
	}

	if (ret == 0 || ret == -FW_ENOMEM)
		ret = nfilters;
	return ret;
}

/**
 *	t4_change_mac - modifies the exact-match filter for a MAC address
 *	@adap: the adapter
 *	@mbox: mailbox to use for the FW command
 *	@viid: the VI id
 *	@idx: index of existing filter for old value of MAC address, or -1
 *	@addr: the new MAC address value
 *	@persist: whether a new MAC allocation should be persistent
 *	@add_smt: if true also add the address to the HW SMT
 *
 *	Modifies an exact-match filter and sets it to the new MAC address.
 *	Note that in general it is not possible to modify the value of a given
 *	filter so the generic way to modify an address filter is to free the one
 *	being used by the old address value and allocate a new filter for the
 *	new address value.  @idx can be -1 if the address is a new addition.
 *
 *	Returns a negative error number or the index of the filter with the new
 *	MAC value.
 */
int t4_change_mac(struct adapter *adap, unsigned int mbox, unsigned int viid,
		  int idx, const u8 *addr, bool persist, bool add_smt)
{
	int ret, mode;
	struct fw_vi_mac_cmd c;
	struct fw_vi_mac_exact *p = c.u.exact;
	unsigned int max_mac_addr = adap->params.arch.mps_tcam_size;

	if (idx < 0)                             /* new allocation */
		idx = persist ? FW_VI_MAC_ADD_PERSIST_MAC : FW_VI_MAC_ADD_MAC;
	mode = add_smt ? FW_VI_MAC_SMT_AND_MPSTCAM : FW_VI_MAC_MPS_TCAM_ENTRY;

	memset(&c, 0, sizeof(c));
	c.op_to_viid = cpu_to_be32(FW_CMD_OP_V(FW_VI_MAC_CMD) |
				   FW_CMD_REQUEST_F | FW_CMD_WRITE_F |
				   FW_VI_MAC_CMD_VIID_V(viid));
	c.freemacs_to_len16 = cpu_to_be32(FW_CMD_LEN16_V(1));
	p->valid_to_idx = cpu_to_be16(FW_VI_MAC_CMD_VALID_F |
				      FW_VI_MAC_CMD_SMAC_RESULT_V(mode) |
				      FW_VI_MAC_CMD_IDX_V(idx));
	memcpy(p->macaddr, addr, sizeof(p->macaddr));

	ret = t4_wr_mbox(adap, mbox, &c, sizeof(c), &c);
	if (ret == 0) {
		ret = FW_VI_MAC_CMD_IDX_G(be16_to_cpu(p->valid_to_idx));
		if (ret >= max_mac_addr)
			ret = -ENOMEM;
	}
	return ret;
}

/**
 *	t4_set_addr_hash - program the MAC inexact-match hash filter
 *	@adap: the adapter
 *	@mbox: mailbox to use for the FW command
 *	@viid: the VI id
 *	@ucast: whether the hash filter should also match unicast addresses
 *	@vec: the value to be written to the hash filter
 *	@sleep_ok: call is allowed to sleep
 *
 *	Sets the 64-bit inexact-match hash filter for a virtual interface.
 */
int t4_set_addr_hash(struct adapter *adap, unsigned int mbox, unsigned int viid,
		     bool ucast, u64 vec, bool sleep_ok)
{
	struct fw_vi_mac_cmd c;

	memset(&c, 0, sizeof(c));
	c.op_to_viid = cpu_to_be32(FW_CMD_OP_V(FW_VI_MAC_CMD) |
				   FW_CMD_REQUEST_F | FW_CMD_WRITE_F |
				   FW_VI_ENABLE_CMD_VIID_V(viid));
	c.freemacs_to_len16 = cpu_to_be32(FW_VI_MAC_CMD_HASHVECEN_F |
					  FW_VI_MAC_CMD_HASHUNIEN_V(ucast) |
					  FW_CMD_LEN16_V(1));
	c.u.hash.hashvec = cpu_to_be64(vec);
	return t4_wr_mbox_meat(adap, mbox, &c, sizeof(c), NULL, sleep_ok);
}

/**
 *      t4_enable_vi_params - enable/disable a virtual interface
 *      @adap: the adapter
 *      @mbox: mailbox to use for the FW command
 *      @viid: the VI id
 *      @rx_en: 1=enable Rx, 0=disable Rx
 *      @tx_en: 1=enable Tx, 0=disable Tx
 *      @dcb_en: 1=enable delivery of Data Center Bridging messages.
 *
 *      Enables/disables a virtual interface.  Note that setting DCB Enable
 *      only makes sense when enabling a Virtual Interface ...
 */
int t4_enable_vi_params(struct adapter *adap, unsigned int mbox,
			unsigned int viid, bool rx_en, bool tx_en, bool dcb_en)
{
	struct fw_vi_enable_cmd c;

	memset(&c, 0, sizeof(c));
	c.op_to_viid = cpu_to_be32(FW_CMD_OP_V(FW_VI_ENABLE_CMD) |
				   FW_CMD_REQUEST_F | FW_CMD_EXEC_F |
				   FW_VI_ENABLE_CMD_VIID_V(viid));
	c.ien_to_len16 = cpu_to_be32(FW_VI_ENABLE_CMD_IEN_V(rx_en) |
				     FW_VI_ENABLE_CMD_EEN_V(tx_en) |
				     FW_VI_ENABLE_CMD_DCB_INFO_V(dcb_en) |
				     FW_LEN16(c));
	return t4_wr_mbox_ns(adap, mbox, &c, sizeof(c), NULL);
}

/**
 *	t4_enable_vi - enable/disable a virtual interface
 *	@adap: the adapter
 *	@mbox: mailbox to use for the FW command
 *	@viid: the VI id
 *	@rx_en: 1=enable Rx, 0=disable Rx
 *	@tx_en: 1=enable Tx, 0=disable Tx
 *
 *	Enables/disables a virtual interface.
 */
int t4_enable_vi(struct adapter *adap, unsigned int mbox, unsigned int viid,
		 bool rx_en, bool tx_en)
{
	return t4_enable_vi_params(adap, mbox, viid, rx_en, tx_en, 0);
}

/**
 *	t4_identify_port - identify a VI's port by blinking its LED
 *	@adap: the adapter
 *	@mbox: mailbox to use for the FW command
 *	@viid: the VI id
 *	@nblinks: how many times to blink LED at 2.5 Hz
 *
 *	Identifies a VI's port by blinking its LED.
 */
int t4_identify_port(struct adapter *adap, unsigned int mbox, unsigned int viid,
		     unsigned int nblinks)
{
	struct fw_vi_enable_cmd c;

	memset(&c, 0, sizeof(c));
	c.op_to_viid = cpu_to_be32(FW_CMD_OP_V(FW_VI_ENABLE_CMD) |
				   FW_CMD_REQUEST_F | FW_CMD_EXEC_F |
				   FW_VI_ENABLE_CMD_VIID_V(viid));
	c.ien_to_len16 = cpu_to_be32(FW_VI_ENABLE_CMD_LED_F | FW_LEN16(c));
	c.blinkdur = cpu_to_be16(nblinks);
	return t4_wr_mbox(adap, mbox, &c, sizeof(c), NULL);
}

/**
 *	t4_iq_free - free an ingress queue and its FLs
 *	@adap: the adapter
 *	@mbox: mailbox to use for the FW command
 *	@pf: the PF owning the queues
 *	@vf: the VF owning the queues
 *	@iqtype: the ingress queue type
 *	@iqid: ingress queue id
 *	@fl0id: FL0 queue id or 0xffff if no attached FL0
 *	@fl1id: FL1 queue id or 0xffff if no attached FL1
 *
 *	Frees an ingress queue and its associated FLs, if any.
 */
int t4_iq_free(struct adapter *adap, unsigned int mbox, unsigned int pf,
	       unsigned int vf, unsigned int iqtype, unsigned int iqid,
	       unsigned int fl0id, unsigned int fl1id)
{
	struct fw_iq_cmd c;

	memset(&c, 0, sizeof(c));
	c.op_to_vfn = cpu_to_be32(FW_CMD_OP_V(FW_IQ_CMD) | FW_CMD_REQUEST_F |
				  FW_CMD_EXEC_F | FW_IQ_CMD_PFN_V(pf) |
				  FW_IQ_CMD_VFN_V(vf));
	c.alloc_to_len16 = cpu_to_be32(FW_IQ_CMD_FREE_F | FW_LEN16(c));
	c.type_to_iqandstindex = cpu_to_be32(FW_IQ_CMD_TYPE_V(iqtype));
	c.iqid = cpu_to_be16(iqid);
	c.fl0id = cpu_to_be16(fl0id);
	c.fl1id = cpu_to_be16(fl1id);
	return t4_wr_mbox(adap, mbox, &c, sizeof(c), NULL);
}

/**
 *	t4_eth_eq_free - free an Ethernet egress queue
 *	@adap: the adapter
 *	@mbox: mailbox to use for the FW command
 *	@pf: the PF owning the queue
 *	@vf: the VF owning the queue
 *	@eqid: egress queue id
 *
 *	Frees an Ethernet egress queue.
 */
int t4_eth_eq_free(struct adapter *adap, unsigned int mbox, unsigned int pf,
		   unsigned int vf, unsigned int eqid)
{
	struct fw_eq_eth_cmd c;

	memset(&c, 0, sizeof(c));
	c.op_to_vfn = cpu_to_be32(FW_CMD_OP_V(FW_EQ_ETH_CMD) |
				  FW_CMD_REQUEST_F | FW_CMD_EXEC_F |
				  FW_EQ_ETH_CMD_PFN_V(pf) |
				  FW_EQ_ETH_CMD_VFN_V(vf));
	c.alloc_to_len16 = cpu_to_be32(FW_EQ_ETH_CMD_FREE_F | FW_LEN16(c));
	c.eqid_pkd = cpu_to_be32(FW_EQ_ETH_CMD_EQID_V(eqid));
	return t4_wr_mbox(adap, mbox, &c, sizeof(c), NULL);
}

/**
 *	t4_ctrl_eq_free - free a control egress queue
 *	@adap: the adapter
 *	@mbox: mailbox to use for the FW command
 *	@pf: the PF owning the queue
 *	@vf: the VF owning the queue
 *	@eqid: egress queue id
 *
 *	Frees a control egress queue.
 */
int t4_ctrl_eq_free(struct adapter *adap, unsigned int mbox, unsigned int pf,
		    unsigned int vf, unsigned int eqid)
{
	struct fw_eq_ctrl_cmd c;

	memset(&c, 0, sizeof(c));
	c.op_to_vfn = cpu_to_be32(FW_CMD_OP_V(FW_EQ_CTRL_CMD) |
				  FW_CMD_REQUEST_F | FW_CMD_EXEC_F |
				  FW_EQ_CTRL_CMD_PFN_V(pf) |
				  FW_EQ_CTRL_CMD_VFN_V(vf));
	c.alloc_to_len16 = cpu_to_be32(FW_EQ_CTRL_CMD_FREE_F | FW_LEN16(c));
	c.cmpliqid_eqid = cpu_to_be32(FW_EQ_CTRL_CMD_EQID_V(eqid));
	return t4_wr_mbox(adap, mbox, &c, sizeof(c), NULL);
}

/**
 *	t4_ofld_eq_free - free an offload egress queue
 *	@adap: the adapter
 *	@mbox: mailbox to use for the FW command
 *	@pf: the PF owning the queue
 *	@vf: the VF owning the queue
 *	@eqid: egress queue id
 *
 *	Frees a control egress queue.
 */
int t4_ofld_eq_free(struct adapter *adap, unsigned int mbox, unsigned int pf,
		    unsigned int vf, unsigned int eqid)
{
	struct fw_eq_ofld_cmd c;

	memset(&c, 0, sizeof(c));
	c.op_to_vfn = cpu_to_be32(FW_CMD_OP_V(FW_EQ_OFLD_CMD) |
				  FW_CMD_REQUEST_F | FW_CMD_EXEC_F |
				  FW_EQ_OFLD_CMD_PFN_V(pf) |
				  FW_EQ_OFLD_CMD_VFN_V(vf));
	c.alloc_to_len16 = cpu_to_be32(FW_EQ_OFLD_CMD_FREE_F | FW_LEN16(c));
	c.eqid_pkd = cpu_to_be32(FW_EQ_OFLD_CMD_EQID_V(eqid));
	return t4_wr_mbox(adap, mbox, &c, sizeof(c), NULL);
}

/**
 *	t4_handle_fw_rpl - process a FW reply message
 *	@adap: the adapter
 *	@rpl: start of the FW message
 *
 *	Processes a FW message, such as link state change messages.
 */
int t4_handle_fw_rpl(struct adapter *adap, const __be64 *rpl)
{
	u8 opcode = *(const u8 *)rpl;

	if (opcode == FW_PORT_CMD) {    /* link/module state change message */
		int speed = 0, fc = 0;
		const struct fw_port_cmd *p = (void *)rpl;
		int chan = FW_PORT_CMD_PORTID_G(be32_to_cpu(p->op_to_portid));
		int port = adap->chan_map[chan];
		struct port_info *pi = adap2pinfo(adap, port);
		struct link_config *lc = &pi->link_cfg;
		u32 stat = be32_to_cpu(p->u.info.lstatus_to_modtype);
		int link_ok = (stat & FW_PORT_CMD_LSTATUS_F) != 0;
		u32 mod = FW_PORT_CMD_MODTYPE_G(stat);

		if (stat & FW_PORT_CMD_RXPAUSE_F)
			fc |= PAUSE_RX;
		if (stat & FW_PORT_CMD_TXPAUSE_F)
			fc |= PAUSE_TX;
		if (stat & FW_PORT_CMD_LSPEED_V(FW_PORT_CAP_SPEED_100M))
			speed = 100;
		else if (stat & FW_PORT_CMD_LSPEED_V(FW_PORT_CAP_SPEED_1G))
			speed = 1000;
		else if (stat & FW_PORT_CMD_LSPEED_V(FW_PORT_CAP_SPEED_10G))
			speed = 10000;
		else if (stat & FW_PORT_CMD_LSPEED_V(FW_PORT_CAP_SPEED_40G))
			speed = 40000;

		if (link_ok != lc->link_ok || speed != lc->speed ||
		    fc != lc->fc) {                    /* something changed */
			lc->link_ok = link_ok;
			lc->speed = speed;
			lc->fc = fc;
			lc->supported = be16_to_cpu(p->u.info.pcap);
			t4_os_link_changed(adap, port, link_ok);
		}
		if (mod != pi->mod_type) {
			pi->mod_type = mod;
			t4_os_portmod_changed(adap, port);
		}
	}
	return 0;
}

static void get_pci_mode(struct adapter *adapter, struct pci_params *p)
{
	u16 val;

	if (pci_is_pcie(adapter->pdev)) {
		pcie_capability_read_word(adapter->pdev, PCI_EXP_LNKSTA, &val);
		p->speed = val & PCI_EXP_LNKSTA_CLS;
		p->width = (val & PCI_EXP_LNKSTA_NLW) >> 4;
	}
}

/**
 *	init_link_config - initialize a link's SW state
 *	@lc: structure holding the link state
 *	@caps: link capabilities
 *
 *	Initializes the SW state maintained for each link, including the link's
 *	capabilities and default speed/flow-control/autonegotiation settings.
 */
static void init_link_config(struct link_config *lc, unsigned int caps)
{
	lc->supported = caps;
	lc->requested_speed = 0;
	lc->speed = 0;
	lc->requested_fc = lc->fc = PAUSE_RX | PAUSE_TX;
	if (lc->supported & FW_PORT_CAP_ANEG) {
		lc->advertising = lc->supported & ADVERT_MASK;
		lc->autoneg = AUTONEG_ENABLE;
		lc->requested_fc |= PAUSE_AUTONEG;
	} else {
		lc->advertising = 0;
		lc->autoneg = AUTONEG_DISABLE;
	}
}

#define CIM_PF_NOACCESS 0xeeeeeeee

int t4_wait_dev_ready(void __iomem *regs)
{
	u32 whoami;

	whoami = readl(regs + PL_WHOAMI_A);
	if (whoami != 0xffffffff && whoami != CIM_PF_NOACCESS)
		return 0;

	msleep(500);
	whoami = readl(regs + PL_WHOAMI_A);
	return (whoami != 0xffffffff && whoami != CIM_PF_NOACCESS ? 0 : -EIO);
}

struct flash_desc {
	u32 vendor_and_model_id;
	u32 size_mb;
};

static int get_flash_params(struct adapter *adap)
{
	/* Table for non-Numonix supported flash parts.  Numonix parts are left
	 * to the preexisting code.  All flash parts have 64KB sectors.
	 */
	static struct flash_desc supported_flash[] = {
		{ 0x150201, 4 << 20 },       /* Spansion 4MB S25FL032P */
	};

	int ret;
	u32 info;

	ret = sf1_write(adap, 1, 1, 0, SF_RD_ID);
	if (!ret)
		ret = sf1_read(adap, 3, 0, 1, &info);
	t4_write_reg(adap, SF_OP_A, 0);                    /* unlock SF */
	if (ret)
		return ret;

	for (ret = 0; ret < ARRAY_SIZE(supported_flash); ++ret)
		if (supported_flash[ret].vendor_and_model_id == info) {
			adap->params.sf_size = supported_flash[ret].size_mb;
			adap->params.sf_nsec =
				adap->params.sf_size / SF_SEC_SIZE;
			return 0;
		}

	if ((info & 0xff) != 0x20)             /* not a Numonix flash */
		return -EINVAL;
	info >>= 16;                           /* log2 of size */
	if (info >= 0x14 && info < 0x18)
		adap->params.sf_nsec = 1 << (info - 16);
	else if (info == 0x18)
		adap->params.sf_nsec = 64;
	else
		return -EINVAL;
	adap->params.sf_size = 1 << info;
	adap->params.sf_fw_start =
		t4_read_reg(adap, CIM_BOOT_CFG_A) & BOOTADDR_M;

	if (adap->params.sf_size < FLASH_MIN_SIZE)
		dev_warn(adap->pdev_dev, "WARNING!!! FLASH size %#x < %#x!!!\n",
			 adap->params.sf_size, FLASH_MIN_SIZE);
	return 0;
}

static void set_pcie_completion_timeout(struct adapter *adapter, u8 range)
{
	u16 val;
	u32 pcie_cap;

	pcie_cap = pci_find_capability(adapter->pdev, PCI_CAP_ID_EXP);
	if (pcie_cap) {
		pci_read_config_word(adapter->pdev,
				     pcie_cap + PCI_EXP_DEVCTL2, &val);
		val &= ~PCI_EXP_DEVCTL2_COMP_TIMEOUT;
		val |= range;
		pci_write_config_word(adapter->pdev,
				      pcie_cap + PCI_EXP_DEVCTL2, val);
	}
}

/**
 *	t4_prep_adapter - prepare SW and HW for operation
 *	@adapter: the adapter
 *	@reset: if true perform a HW reset
 *
 *	Initialize adapter SW state for the various HW modules, set initial
 *	values for some adapter tunables, take PHYs out of reset, and
 *	initialize the MDIO interface.
 */
int t4_prep_adapter(struct adapter *adapter)
{
	int ret, ver;
	uint16_t device_id;
	u32 pl_rev;

	get_pci_mode(adapter, &adapter->params.pci);
	pl_rev = REV_G(t4_read_reg(adapter, PL_REV_A));

	ret = get_flash_params(adapter);
	if (ret < 0) {
		dev_err(adapter->pdev_dev, "error %d identifying flash\n", ret);
		return ret;
	}

	/* Retrieve adapter's device ID
	 */
	pci_read_config_word(adapter->pdev, PCI_DEVICE_ID, &device_id);
	ver = device_id >> 12;
	adapter->params.chip = 0;
	switch (ver) {
	case CHELSIO_T4:
		adapter->params.chip |= CHELSIO_CHIP_CODE(CHELSIO_T4, pl_rev);
		adapter->params.arch.sge_fl_db = DBPRIO_F;
		adapter->params.arch.mps_tcam_size =
				 NUM_MPS_CLS_SRAM_L_INSTANCES;
		adapter->params.arch.mps_rplc_size = 128;
		adapter->params.arch.nchan = NCHAN;
		adapter->params.arch.vfcount = 128;
		break;
	case CHELSIO_T5:
		adapter->params.chip |= CHELSIO_CHIP_CODE(CHELSIO_T5, pl_rev);
		adapter->params.arch.sge_fl_db = DBPRIO_F | DBTYPE_F;
		adapter->params.arch.mps_tcam_size =
				 NUM_MPS_T5_CLS_SRAM_L_INSTANCES;
		adapter->params.arch.mps_rplc_size = 128;
		adapter->params.arch.nchan = NCHAN;
		adapter->params.arch.vfcount = 128;
		break;
	case CHELSIO_T6:
		adapter->params.chip |= CHELSIO_CHIP_CODE(CHELSIO_T6, pl_rev);
		adapter->params.arch.sge_fl_db = 0;
		adapter->params.arch.mps_tcam_size =
				 NUM_MPS_T5_CLS_SRAM_L_INSTANCES;
		adapter->params.arch.mps_rplc_size = 256;
		adapter->params.arch.nchan = 2;
		adapter->params.arch.vfcount = 256;
		break;
	default:
		dev_err(adapter->pdev_dev, "Device %d is not supported\n",
			device_id);
		return -EINVAL;
	}

	adapter->params.cim_la_size = CIMLA_SIZE;
	init_cong_ctrl(adapter->params.a_wnd, adapter->params.b_wnd);

	/*
	 * Default port for debugging in case we can't reach FW.
	 */
	adapter->params.nports = 1;
	adapter->params.portvec = 1;
	adapter->params.vpd.cclk = 50000;

	/* Set pci completion timeout value to 4 seconds. */
	set_pcie_completion_timeout(adapter, 0xd);
	return 0;
}

/**
 *	t4_bar2_sge_qregs - return BAR2 SGE Queue register information
 *	@adapter: the adapter
 *	@qid: the Queue ID
 *	@qtype: the Ingress or Egress type for @qid
 *	@user: true if this request is for a user mode queue
 *	@pbar2_qoffset: BAR2 Queue Offset
 *	@pbar2_qid: BAR2 Queue ID or 0 for Queue ID inferred SGE Queues
 *
 *	Returns the BAR2 SGE Queue Registers information associated with the
 *	indicated Absolute Queue ID.  These are passed back in return value
 *	pointers.  @qtype should be T4_BAR2_QTYPE_EGRESS for Egress Queue
 *	and T4_BAR2_QTYPE_INGRESS for Ingress Queues.
 *
 *	This may return an error which indicates that BAR2 SGE Queue
 *	registers aren't available.  If an error is not returned, then the
 *	following values are returned:
 *
 *	  *@pbar2_qoffset: the BAR2 Offset of the @qid Registers
 *	  *@pbar2_qid: the BAR2 SGE Queue ID or 0 of @qid
 *
 *	If the returned BAR2 Queue ID is 0, then BAR2 SGE registers which
 *	require the "Inferred Queue ID" ability may be used.  E.g. the
 *	Write Combining Doorbell Buffer. If the BAR2 Queue ID is not 0,
 *	then these "Inferred Queue ID" register may not be used.
 */
int t4_bar2_sge_qregs(struct adapter *adapter,
		      unsigned int qid,
		      enum t4_bar2_qtype qtype,
		      int user,
		      u64 *pbar2_qoffset,
		      unsigned int *pbar2_qid)
{
	unsigned int page_shift, page_size, qpp_shift, qpp_mask;
	u64 bar2_page_offset, bar2_qoffset;
	unsigned int bar2_qid, bar2_qid_offset, bar2_qinferred;

	/* T4 doesn't support BAR2 SGE Queue registers for kernel mode queues */
	if (!user && is_t4(adapter->params.chip))
		return -EINVAL;

	/* Get our SGE Page Size parameters.
	 */
	page_shift = adapter->params.sge.hps + 10;
	page_size = 1 << page_shift;

	/* Get the right Queues per Page parameters for our Queue.
	 */
	qpp_shift = (qtype == T4_BAR2_QTYPE_EGRESS
		     ? adapter->params.sge.eq_qpp
		     : adapter->params.sge.iq_qpp);
	qpp_mask = (1 << qpp_shift) - 1;

	/*  Calculate the basics of the BAR2 SGE Queue register area:
	 *  o The BAR2 page the Queue registers will be in.
	 *  o The BAR2 Queue ID.
	 *  o The BAR2 Queue ID Offset into the BAR2 page.
	 */
	bar2_page_offset = ((u64)(qid >> qpp_shift) << page_shift);
	bar2_qid = qid & qpp_mask;
	bar2_qid_offset = bar2_qid * SGE_UDB_SIZE;

	/* If the BAR2 Queue ID Offset is less than the Page Size, then the
	 * hardware will infer the Absolute Queue ID simply from the writes to
	 * the BAR2 Queue ID Offset within the BAR2 Page (and we need to use a
	 * BAR2 Queue ID of 0 for those writes).  Otherwise, we'll simply
	 * write to the first BAR2 SGE Queue Area within the BAR2 Page with
	 * the BAR2 Queue ID and the hardware will infer the Absolute Queue ID
	 * from the BAR2 Page and BAR2 Queue ID.
	 *
	 * One important censequence of this is that some BAR2 SGE registers
	 * have a "Queue ID" field and we can write the BAR2 SGE Queue ID
	 * there.  But other registers synthesize the SGE Queue ID purely
	 * from the writes to the registers -- the Write Combined Doorbell
	 * Buffer is a good example.  These BAR2 SGE Registers are only
	 * available for those BAR2 SGE Register areas where the SGE Absolute
	 * Queue ID can be inferred from simple writes.
	 */
	bar2_qoffset = bar2_page_offset;
	bar2_qinferred = (bar2_qid_offset < page_size);
	if (bar2_qinferred) {
		bar2_qoffset += bar2_qid_offset;
		bar2_qid = 0;
	}

	*pbar2_qoffset = bar2_qoffset;
	*pbar2_qid = bar2_qid;
	return 0;
}

/**
 *	t4_init_devlog_params - initialize adapter->params.devlog
 *	@adap: the adapter
 *
 *	Initialize various fields of the adapter's Firmware Device Log
 *	Parameters structure.
 */
int t4_init_devlog_params(struct adapter *adap)
{
	struct devlog_params *dparams = &adap->params.devlog;
	u32 pf_dparams;
	unsigned int devlog_meminfo;
	struct fw_devlog_cmd devlog_cmd;
	int ret;

	/* If we're dealing with newer firmware, the Device Log Paramerters
	 * are stored in a designated register which allows us to access the
	 * Device Log even if we can't talk to the firmware.
	 */
	pf_dparams =
		t4_read_reg(adap, PCIE_FW_REG(PCIE_FW_PF_A, PCIE_FW_PF_DEVLOG));
	if (pf_dparams) {
		unsigned int nentries, nentries128;

		dparams->memtype = PCIE_FW_PF_DEVLOG_MEMTYPE_G(pf_dparams);
		dparams->start = PCIE_FW_PF_DEVLOG_ADDR16_G(pf_dparams) << 4;

		nentries128 = PCIE_FW_PF_DEVLOG_NENTRIES128_G(pf_dparams);
		nentries = (nentries128 + 1) * 128;
		dparams->size = nentries * sizeof(struct fw_devlog_e);

		return 0;
	}

	/* Otherwise, ask the firmware for it's Device Log Parameters.
	 */
	memset(&devlog_cmd, 0, sizeof(devlog_cmd));
	devlog_cmd.op_to_write = cpu_to_be32(FW_CMD_OP_V(FW_DEVLOG_CMD) |
					     FW_CMD_REQUEST_F | FW_CMD_READ_F);
	devlog_cmd.retval_len16 = cpu_to_be32(FW_LEN16(devlog_cmd));
	ret = t4_wr_mbox(adap, adap->mbox, &devlog_cmd, sizeof(devlog_cmd),
			 &devlog_cmd);
	if (ret)
		return ret;

	devlog_meminfo =
		be32_to_cpu(devlog_cmd.memtype_devlog_memaddr16_devlog);
	dparams->memtype = FW_DEVLOG_CMD_MEMTYPE_DEVLOG_G(devlog_meminfo);
	dparams->start = FW_DEVLOG_CMD_MEMADDR16_DEVLOG_G(devlog_meminfo) << 4;
	dparams->size = be32_to_cpu(devlog_cmd.memsize_devlog);

	return 0;
}

/**
 *	t4_init_sge_params - initialize adap->params.sge
 *	@adapter: the adapter
 *
 *	Initialize various fields of the adapter's SGE Parameters structure.
 */
int t4_init_sge_params(struct adapter *adapter)
{
	struct sge_params *sge_params = &adapter->params.sge;
	u32 hps, qpp;
	unsigned int s_hps, s_qpp;

	/* Extract the SGE Page Size for our PF.
	 */
	hps = t4_read_reg(adapter, SGE_HOST_PAGE_SIZE_A);
	s_hps = (HOSTPAGESIZEPF0_S +
		 (HOSTPAGESIZEPF1_S - HOSTPAGESIZEPF0_S) * adapter->pf);
	sge_params->hps = ((hps >> s_hps) & HOSTPAGESIZEPF0_M);

	/* Extract the SGE Egress and Ingess Queues Per Page for our PF.
	 */
	s_qpp = (QUEUESPERPAGEPF0_S +
		(QUEUESPERPAGEPF1_S - QUEUESPERPAGEPF0_S) * adapter->pf);
	qpp = t4_read_reg(adapter, SGE_EGRESS_QUEUES_PER_PAGE_PF_A);
	sge_params->eq_qpp = ((qpp >> s_qpp) & QUEUESPERPAGEPF0_M);
	qpp = t4_read_reg(adapter, SGE_INGRESS_QUEUES_PER_PAGE_PF_A);
	sge_params->iq_qpp = ((qpp >> s_qpp) & QUEUESPERPAGEPF0_M);

	return 0;
}

/**
 *      t4_init_tp_params - initialize adap->params.tp
 *      @adap: the adapter
 *
 *      Initialize various fields of the adapter's TP Parameters structure.
 */
int t4_init_tp_params(struct adapter *adap)
{
	int chan;
	u32 v;

	v = t4_read_reg(adap, TP_TIMER_RESOLUTION_A);
	adap->params.tp.tre = TIMERRESOLUTION_G(v);
	adap->params.tp.dack_re = DELAYEDACKRESOLUTION_G(v);

	/* MODQ_REQ_MAP defaults to setting queues 0-3 to chan 0-3 */
	for (chan = 0; chan < NCHAN; chan++)
		adap->params.tp.tx_modq[chan] = chan;

	/* Cache the adapter's Compressed Filter Mode and global Incress
	 * Configuration.
	 */
	if (t4_use_ldst(adap)) {
		t4_fw_tp_pio_rw(adap, &adap->params.tp.vlan_pri_map, 1,
				TP_VLAN_PRI_MAP_A, 1);
		t4_fw_tp_pio_rw(adap, &adap->params.tp.ingress_config, 1,
				TP_INGRESS_CONFIG_A, 1);
	} else {
		t4_read_indirect(adap, TP_PIO_ADDR_A, TP_PIO_DATA_A,
				 &adap->params.tp.vlan_pri_map, 1,
				 TP_VLAN_PRI_MAP_A);
		t4_read_indirect(adap, TP_PIO_ADDR_A, TP_PIO_DATA_A,
				 &adap->params.tp.ingress_config, 1,
				 TP_INGRESS_CONFIG_A);
	}

	/* Now that we have TP_VLAN_PRI_MAP cached, we can calculate the field
	 * shift positions of several elements of the Compressed Filter Tuple
	 * for this adapter which we need frequently ...
	 */
	adap->params.tp.vlan_shift = t4_filter_field_shift(adap, VLAN_F);
	adap->params.tp.vnic_shift = t4_filter_field_shift(adap, VNIC_ID_F);
	adap->params.tp.port_shift = t4_filter_field_shift(adap, PORT_F);
	adap->params.tp.protocol_shift = t4_filter_field_shift(adap,
							       PROTOCOL_F);

	/* If TP_INGRESS_CONFIG.VNID == 0, then TP_VLAN_PRI_MAP.VNIC_ID
	 * represents the presence of an Outer VLAN instead of a VNIC ID.
	 */
	if ((adap->params.tp.ingress_config & VNIC_F) == 0)
		adap->params.tp.vnic_shift = -1;

	return 0;
}

/**
 *      t4_filter_field_shift - calculate filter field shift
 *      @adap: the adapter
 *      @filter_sel: the desired field (from TP_VLAN_PRI_MAP bits)
 *
 *      Return the shift position of a filter field within the Compressed
 *      Filter Tuple.  The filter field is specified via its selection bit
 *      within TP_VLAN_PRI_MAL (filter mode).  E.g. F_VLAN.
 */
int t4_filter_field_shift(const struct adapter *adap, int filter_sel)
{
	unsigned int filter_mode = adap->params.tp.vlan_pri_map;
	unsigned int sel;
	int field_shift;

	if ((filter_mode & filter_sel) == 0)
		return -1;

	for (sel = 1, field_shift = 0; sel < filter_sel; sel <<= 1) {
		switch (filter_mode & sel) {
		case FCOE_F:
			field_shift += FT_FCOE_W;
			break;
		case PORT_F:
			field_shift += FT_PORT_W;
			break;
		case VNIC_ID_F:
			field_shift += FT_VNIC_ID_W;
			break;
		case VLAN_F:
			field_shift += FT_VLAN_W;
			break;
		case TOS_F:
			field_shift += FT_TOS_W;
			break;
		case PROTOCOL_F:
			field_shift += FT_PROTOCOL_W;
			break;
		case ETHERTYPE_F:
			field_shift += FT_ETHERTYPE_W;
			break;
		case MACMATCH_F:
			field_shift += FT_MACMATCH_W;
			break;
		case MPSHITTYPE_F:
			field_shift += FT_MPSHITTYPE_W;
			break;
		case FRAGMENTATION_F:
			field_shift += FT_FRAGMENTATION_W;
			break;
		}
	}
	return field_shift;
}

int t4_init_rss_mode(struct adapter *adap, int mbox)
{
	int i, ret;
	struct fw_rss_vi_config_cmd rvc;

	memset(&rvc, 0, sizeof(rvc));

	for_each_port(adap, i) {
		struct port_info *p = adap2pinfo(adap, i);

		rvc.op_to_viid =
			cpu_to_be32(FW_CMD_OP_V(FW_RSS_VI_CONFIG_CMD) |
				    FW_CMD_REQUEST_F | FW_CMD_READ_F |
				    FW_RSS_VI_CONFIG_CMD_VIID_V(p->viid));
		rvc.retval_len16 = cpu_to_be32(FW_LEN16(rvc));
		ret = t4_wr_mbox(adap, mbox, &rvc, sizeof(rvc), &rvc);
		if (ret)
			return ret;
		p->rss_mode = be32_to_cpu(rvc.u.basicvirtual.defaultq_to_udpen);
	}
	return 0;
}

int t4_port_init(struct adapter *adap, int mbox, int pf, int vf)
{
	u8 addr[6];
	int ret, i, j = 0;
	struct fw_port_cmd c;
	struct fw_rss_vi_config_cmd rvc;

	memset(&c, 0, sizeof(c));
	memset(&rvc, 0, sizeof(rvc));

	for_each_port(adap, i) {
		unsigned int rss_size;
		struct port_info *p = adap2pinfo(adap, i);

		while ((adap->params.portvec & (1 << j)) == 0)
			j++;

		c.op_to_portid = cpu_to_be32(FW_CMD_OP_V(FW_PORT_CMD) |
					     FW_CMD_REQUEST_F | FW_CMD_READ_F |
					     FW_PORT_CMD_PORTID_V(j));
		c.action_to_len16 = cpu_to_be32(
			FW_PORT_CMD_ACTION_V(FW_PORT_ACTION_GET_PORT_INFO) |
			FW_LEN16(c));
		ret = t4_wr_mbox(adap, mbox, &c, sizeof(c), &c);
		if (ret)
			return ret;

		ret = t4_alloc_vi(adap, mbox, j, pf, vf, 1, addr, &rss_size);
		if (ret < 0)
			return ret;

		p->viid = ret;
		p->tx_chan = j;
		p->lport = j;
		p->rss_size = rss_size;
		memcpy(adap->port[i]->dev_addr, addr, ETH_ALEN);
		adap->port[i]->dev_port = j;

		ret = be32_to_cpu(c.u.info.lstatus_to_modtype);
		p->mdio_addr = (ret & FW_PORT_CMD_MDIOCAP_F) ?
			FW_PORT_CMD_MDIOADDR_G(ret) : -1;
		p->port_type = FW_PORT_CMD_PTYPE_G(ret);
		p->mod_type = FW_PORT_MOD_TYPE_NA;

		rvc.op_to_viid =
			cpu_to_be32(FW_CMD_OP_V(FW_RSS_VI_CONFIG_CMD) |
				    FW_CMD_REQUEST_F | FW_CMD_READ_F |
				    FW_RSS_VI_CONFIG_CMD_VIID(p->viid));
		rvc.retval_len16 = cpu_to_be32(FW_LEN16(rvc));
		ret = t4_wr_mbox(adap, mbox, &rvc, sizeof(rvc), &rvc);
		if (ret)
			return ret;
		p->rss_mode = be32_to_cpu(rvc.u.basicvirtual.defaultq_to_udpen);

		init_link_config(&p->link_cfg, be16_to_cpu(c.u.info.pcap));
		j++;
	}
	return 0;
}

/**
 *	t4_read_cimq_cfg - read CIM queue configuration
 *	@adap: the adapter
 *	@base: holds the queue base addresses in bytes
 *	@size: holds the queue sizes in bytes
 *	@thres: holds the queue full thresholds in bytes
 *
 *	Returns the current configuration of the CIM queues, starting with
 *	the IBQs, then the OBQs.
 */
void t4_read_cimq_cfg(struct adapter *adap, u16 *base, u16 *size, u16 *thres)
{
	unsigned int i, v;
	int cim_num_obq = is_t4(adap->params.chip) ?
				CIM_NUM_OBQ : CIM_NUM_OBQ_T5;

	for (i = 0; i < CIM_NUM_IBQ; i++) {
		t4_write_reg(adap, CIM_QUEUE_CONFIG_REF_A, IBQSELECT_F |
			     QUENUMSELECT_V(i));
		v = t4_read_reg(adap, CIM_QUEUE_CONFIG_CTRL_A);
		/* value is in 256-byte units */
		*base++ = CIMQBASE_G(v) * 256;
		*size++ = CIMQSIZE_G(v) * 256;
		*thres++ = QUEFULLTHRSH_G(v) * 8; /* 8-byte unit */
	}
	for (i = 0; i < cim_num_obq; i++) {
		t4_write_reg(adap, CIM_QUEUE_CONFIG_REF_A, OBQSELECT_F |
			     QUENUMSELECT_V(i));
		v = t4_read_reg(adap, CIM_QUEUE_CONFIG_CTRL_A);
		/* value is in 256-byte units */
		*base++ = CIMQBASE_G(v) * 256;
		*size++ = CIMQSIZE_G(v) * 256;
	}
}

/**
 *	t4_read_cim_ibq - read the contents of a CIM inbound queue
 *	@adap: the adapter
 *	@qid: the queue index
 *	@data: where to store the queue contents
 *	@n: capacity of @data in 32-bit words
 *
 *	Reads the contents of the selected CIM queue starting at address 0 up
 *	to the capacity of @data.  @n must be a multiple of 4.  Returns < 0 on
 *	error and the number of 32-bit words actually read on success.
 */
int t4_read_cim_ibq(struct adapter *adap, unsigned int qid, u32 *data, size_t n)
{
	int i, err, attempts;
	unsigned int addr;
	const unsigned int nwords = CIM_IBQ_SIZE * 4;

	if (qid > 5 || (n & 3))
		return -EINVAL;

	addr = qid * nwords;
	if (n > nwords)
		n = nwords;

	/* It might take 3-10ms before the IBQ debug read access is allowed.
	 * Wait for 1 Sec with a delay of 1 usec.
	 */
	attempts = 1000000;

	for (i = 0; i < n; i++, addr++) {
		t4_write_reg(adap, CIM_IBQ_DBG_CFG_A, IBQDBGADDR_V(addr) |
			     IBQDBGEN_F);
		err = t4_wait_op_done(adap, CIM_IBQ_DBG_CFG_A, IBQDBGBUSY_F, 0,
				      attempts, 1);
		if (err)
			return err;
		*data++ = t4_read_reg(adap, CIM_IBQ_DBG_DATA_A);
	}
	t4_write_reg(adap, CIM_IBQ_DBG_CFG_A, 0);
	return i;
}

/**
 *	t4_read_cim_obq - read the contents of a CIM outbound queue
 *	@adap: the adapter
 *	@qid: the queue index
 *	@data: where to store the queue contents
 *	@n: capacity of @data in 32-bit words
 *
 *	Reads the contents of the selected CIM queue starting at address 0 up
 *	to the capacity of @data.  @n must be a multiple of 4.  Returns < 0 on
 *	error and the number of 32-bit words actually read on success.
 */
int t4_read_cim_obq(struct adapter *adap, unsigned int qid, u32 *data, size_t n)
{
	int i, err;
	unsigned int addr, v, nwords;
	int cim_num_obq = is_t4(adap->params.chip) ?
				CIM_NUM_OBQ : CIM_NUM_OBQ_T5;

	if ((qid > (cim_num_obq - 1)) || (n & 3))
		return -EINVAL;

	t4_write_reg(adap, CIM_QUEUE_CONFIG_REF_A, OBQSELECT_F |
		     QUENUMSELECT_V(qid));
	v = t4_read_reg(adap, CIM_QUEUE_CONFIG_CTRL_A);

	addr = CIMQBASE_G(v) * 64;    /* muliple of 256 -> muliple of 4 */
	nwords = CIMQSIZE_G(v) * 64;  /* same */
	if (n > nwords)
		n = nwords;

	for (i = 0; i < n; i++, addr++) {
		t4_write_reg(adap, CIM_OBQ_DBG_CFG_A, OBQDBGADDR_V(addr) |
			     OBQDBGEN_F);
		err = t4_wait_op_done(adap, CIM_OBQ_DBG_CFG_A, OBQDBGBUSY_F, 0,
				      2, 1);
		if (err)
			return err;
		*data++ = t4_read_reg(adap, CIM_OBQ_DBG_DATA_A);
	}
	t4_write_reg(adap, CIM_OBQ_DBG_CFG_A, 0);
	return i;
}

/**
 *	t4_cim_read - read a block from CIM internal address space
 *	@adap: the adapter
 *	@addr: the start address within the CIM address space
 *	@n: number of words to read
 *	@valp: where to store the result
 *
 *	Reads a block of 4-byte words from the CIM intenal address space.
 */
int t4_cim_read(struct adapter *adap, unsigned int addr, unsigned int n,
		unsigned int *valp)
{
	int ret = 0;

	if (t4_read_reg(adap, CIM_HOST_ACC_CTRL_A) & HOSTBUSY_F)
		return -EBUSY;

	for ( ; !ret && n--; addr += 4) {
		t4_write_reg(adap, CIM_HOST_ACC_CTRL_A, addr);
		ret = t4_wait_op_done(adap, CIM_HOST_ACC_CTRL_A, HOSTBUSY_F,
				      0, 5, 2);
		if (!ret)
			*valp++ = t4_read_reg(adap, CIM_HOST_ACC_DATA_A);
	}
	return ret;
}

/**
 *	t4_cim_write - write a block into CIM internal address space
 *	@adap: the adapter
 *	@addr: the start address within the CIM address space
 *	@n: number of words to write
 *	@valp: set of values to write
 *
 *	Writes a block of 4-byte words into the CIM intenal address space.
 */
int t4_cim_write(struct adapter *adap, unsigned int addr, unsigned int n,
		 const unsigned int *valp)
{
	int ret = 0;

	if (t4_read_reg(adap, CIM_HOST_ACC_CTRL_A) & HOSTBUSY_F)
		return -EBUSY;

	for ( ; !ret && n--; addr += 4) {
		t4_write_reg(adap, CIM_HOST_ACC_DATA_A, *valp++);
		t4_write_reg(adap, CIM_HOST_ACC_CTRL_A, addr | HOSTWRITE_F);
		ret = t4_wait_op_done(adap, CIM_HOST_ACC_CTRL_A, HOSTBUSY_F,
				      0, 5, 2);
	}
	return ret;
}

static int t4_cim_write1(struct adapter *adap, unsigned int addr,
			 unsigned int val)
{
	return t4_cim_write(adap, addr, 1, &val);
}

/**
 *	t4_cim_read_la - read CIM LA capture buffer
 *	@adap: the adapter
 *	@la_buf: where to store the LA data
 *	@wrptr: the HW write pointer within the capture buffer
 *
 *	Reads the contents of the CIM LA buffer with the most recent entry at
 *	the end	of the returned data and with the entry at @wrptr first.
 *	We try to leave the LA in the running state we find it in.
 */
int t4_cim_read_la(struct adapter *adap, u32 *la_buf, unsigned int *wrptr)
{
	int i, ret;
	unsigned int cfg, val, idx;

	ret = t4_cim_read(adap, UP_UP_DBG_LA_CFG_A, 1, &cfg);
	if (ret)
		return ret;

	if (cfg & UPDBGLAEN_F) {	/* LA is running, freeze it */
		ret = t4_cim_write1(adap, UP_UP_DBG_LA_CFG_A, 0);
		if (ret)
			return ret;
	}

	ret = t4_cim_read(adap, UP_UP_DBG_LA_CFG_A, 1, &val);
	if (ret)
		goto restart;

	idx = UPDBGLAWRPTR_G(val);
	if (wrptr)
		*wrptr = idx;

	for (i = 0; i < adap->params.cim_la_size; i++) {
		ret = t4_cim_write1(adap, UP_UP_DBG_LA_CFG_A,
				    UPDBGLARDPTR_V(idx) | UPDBGLARDEN_F);
		if (ret)
			break;
		ret = t4_cim_read(adap, UP_UP_DBG_LA_CFG_A, 1, &val);
		if (ret)
			break;
		if (val & UPDBGLARDEN_F) {
			ret = -ETIMEDOUT;
			break;
		}
		ret = t4_cim_read(adap, UP_UP_DBG_LA_DATA_A, 1, &la_buf[i]);
		if (ret)
			break;
		idx = (idx + 1) & UPDBGLARDPTR_M;
	}
restart:
	if (cfg & UPDBGLAEN_F) {
		int r = t4_cim_write1(adap, UP_UP_DBG_LA_CFG_A,
				      cfg & ~UPDBGLARDEN_F);
		if (!ret)
			ret = r;
	}
	return ret;
}

/**
 *	t4_tp_read_la - read TP LA capture buffer
 *	@adap: the adapter
 *	@la_buf: where to store the LA data
 *	@wrptr: the HW write pointer within the capture buffer
 *
 *	Reads the contents of the TP LA buffer with the most recent entry at
 *	the end	of the returned data and with the entry at @wrptr first.
 *	We leave the LA in the running state we find it in.
 */
void t4_tp_read_la(struct adapter *adap, u64 *la_buf, unsigned int *wrptr)
{
	bool last_incomplete;
	unsigned int i, cfg, val, idx;

	cfg = t4_read_reg(adap, TP_DBG_LA_CONFIG_A) & 0xffff;
	if (cfg & DBGLAENABLE_F)			/* freeze LA */
		t4_write_reg(adap, TP_DBG_LA_CONFIG_A,
			     adap->params.tp.la_mask | (cfg ^ DBGLAENABLE_F));

	val = t4_read_reg(adap, TP_DBG_LA_CONFIG_A);
	idx = DBGLAWPTR_G(val);
	last_incomplete = DBGLAMODE_G(val) >= 2 && (val & DBGLAWHLF_F) == 0;
	if (last_incomplete)
		idx = (idx + 1) & DBGLARPTR_M;
	if (wrptr)
		*wrptr = idx;

	val &= 0xffff;
	val &= ~DBGLARPTR_V(DBGLARPTR_M);
	val |= adap->params.tp.la_mask;

	for (i = 0; i < TPLA_SIZE; i++) {
		t4_write_reg(adap, TP_DBG_LA_CONFIG_A, DBGLARPTR_V(idx) | val);
		la_buf[i] = t4_read_reg64(adap, TP_DBG_LA_DATAL_A);
		idx = (idx + 1) & DBGLARPTR_M;
	}

	/* Wipe out last entry if it isn't valid */
	if (last_incomplete)
		la_buf[TPLA_SIZE - 1] = ~0ULL;

	if (cfg & DBGLAENABLE_F)                    /* restore running state */
		t4_write_reg(adap, TP_DBG_LA_CONFIG_A,
			     cfg | adap->params.tp.la_mask);
}

/* SGE Hung Ingress DMA Warning Threshold time and Warning Repeat Rate (in
 * seconds).  If we find one of the SGE Ingress DMA State Machines in the same
 * state for more than the Warning Threshold then we'll issue a warning about
 * a potential hang.  We'll repeat the warning as the SGE Ingress DMA Channel
 * appears to be hung every Warning Repeat second till the situation clears.
 * If the situation clears, we'll note that as well.
 */
#define SGE_IDMA_WARN_THRESH 1
#define SGE_IDMA_WARN_REPEAT 300

/**
 *	t4_idma_monitor_init - initialize SGE Ingress DMA Monitor
 *	@adapter: the adapter
 *	@idma: the adapter IDMA Monitor state
 *
 *	Initialize the state of an SGE Ingress DMA Monitor.
 */
void t4_idma_monitor_init(struct adapter *adapter,
			  struct sge_idma_monitor_state *idma)
{
	/* Initialize the state variables for detecting an SGE Ingress DMA
	 * hang.  The SGE has internal counters which count up on each clock
	 * tick whenever the SGE finds its Ingress DMA State Engines in the
	 * same state they were on the previous clock tick.  The clock used is
	 * the Core Clock so we have a limit on the maximum "time" they can
	 * record; typically a very small number of seconds.  For instance,
	 * with a 600MHz Core Clock, we can only count up to a bit more than
	 * 7s.  So we'll synthesize a larger counter in order to not run the
	 * risk of having the "timers" overflow and give us the flexibility to
	 * maintain a Hung SGE State Machine of our own which operates across
	 * a longer time frame.
	 */
	idma->idma_1s_thresh = core_ticks_per_usec(adapter) * 1000000; /* 1s */
	idma->idma_stalled[0] = 0;
	idma->idma_stalled[1] = 0;
}

/**
 *	t4_idma_monitor - monitor SGE Ingress DMA state
 *	@adapter: the adapter
 *	@idma: the adapter IDMA Monitor state
 *	@hz: number of ticks/second
 *	@ticks: number of ticks since the last IDMA Monitor call
 */
void t4_idma_monitor(struct adapter *adapter,
		     struct sge_idma_monitor_state *idma,
		     int hz, int ticks)
{
	int i, idma_same_state_cnt[2];

	 /* Read the SGE Debug Ingress DMA Same State Count registers.  These
	  * are counters inside the SGE which count up on each clock when the
	  * SGE finds its Ingress DMA State Engines in the same states they
	  * were in the previous clock.  The counters will peg out at
	  * 0xffffffff without wrapping around so once they pass the 1s
	  * threshold they'll stay above that till the IDMA state changes.
	  */
	t4_write_reg(adapter, SGE_DEBUG_INDEX_A, 13);
	idma_same_state_cnt[0] = t4_read_reg(adapter, SGE_DEBUG_DATA_HIGH_A);
	idma_same_state_cnt[1] = t4_read_reg(adapter, SGE_DEBUG_DATA_LOW_A);

	for (i = 0; i < 2; i++) {
		u32 debug0, debug11;

		/* If the Ingress DMA Same State Counter ("timer") is less
		 * than 1s, then we can reset our synthesized Stall Timer and
		 * continue.  If we have previously emitted warnings about a
		 * potential stalled Ingress Queue, issue a note indicating
		 * that the Ingress Queue has resumed forward progress.
		 */
		if (idma_same_state_cnt[i] < idma->idma_1s_thresh) {
			if (idma->idma_stalled[i] >= SGE_IDMA_WARN_THRESH * hz)
				dev_warn(adapter->pdev_dev, "SGE idma%d, queue %u, "
					 "resumed after %d seconds\n",
					 i, idma->idma_qid[i],
					 idma->idma_stalled[i] / hz);
			idma->idma_stalled[i] = 0;
			continue;
		}

		/* Synthesize an SGE Ingress DMA Same State Timer in the Hz
		 * domain.  The first time we get here it'll be because we
		 * passed the 1s Threshold; each additional time it'll be
		 * because the RX Timer Callback is being fired on its regular
		 * schedule.
		 *
		 * If the stall is below our Potential Hung Ingress Queue
		 * Warning Threshold, continue.
		 */
		if (idma->idma_stalled[i] == 0) {
			idma->idma_stalled[i] = hz;
			idma->idma_warn[i] = 0;
		} else {
			idma->idma_stalled[i] += ticks;
			idma->idma_warn[i] -= ticks;
		}

		if (idma->idma_stalled[i] < SGE_IDMA_WARN_THRESH * hz)
			continue;

		/* We'll issue a warning every SGE_IDMA_WARN_REPEAT seconds.
		 */
		if (idma->idma_warn[i] > 0)
			continue;
		idma->idma_warn[i] = SGE_IDMA_WARN_REPEAT * hz;

		/* Read and save the SGE IDMA State and Queue ID information.
		 * We do this every time in case it changes across time ...
		 * can't be too careful ...
		 */
		t4_write_reg(adapter, SGE_DEBUG_INDEX_A, 0);
		debug0 = t4_read_reg(adapter, SGE_DEBUG_DATA_LOW_A);
		idma->idma_state[i] = (debug0 >> (i * 9)) & 0x3f;

		t4_write_reg(adapter, SGE_DEBUG_INDEX_A, 11);
		debug11 = t4_read_reg(adapter, SGE_DEBUG_DATA_LOW_A);
		idma->idma_qid[i] = (debug11 >> (i * 16)) & 0xffff;

		dev_warn(adapter->pdev_dev, "SGE idma%u, queue %u, potentially stuck in "
			 "state %u for %d seconds (debug0=%#x, debug11=%#x)\n",
			 i, idma->idma_qid[i], idma->idma_state[i],
			 idma->idma_stalled[i] / hz,
			 debug0, debug11);
		t4_sge_decode_idma_state(adapter, idma->idma_state[i]);
	}
}
