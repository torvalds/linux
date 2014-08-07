/*
 * This file is part of the Chelsio T4 Ethernet driver for Linux.
 *
 * Copyright (c) 2003-2010 Chelsio Communications, Inc. All rights reserved.
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
#include "t4fw_api.h"

static int t4_fw_upgrade(struct adapter *adap, unsigned int mbox,
			 const u8 *fw_data, unsigned int size, int force);
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
		  asrt.u.assert.filename_0_7, ntohl(asrt.u.assert.line),
		  ntohl(asrt.u.assert.x), ntohl(asrt.u.assert.y));
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
 *	t4_wr_mbox_meat - send a command to FW through the given mailbox
 *	@adap: the adapter
 *	@mbox: index of the mailbox to use
 *	@cmd: the command to write
 *	@size: command length in bytes
 *	@rpl: where to optionally store the reply
 *	@sleep_ok: if true we may sleep while awaiting command completion
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
int t4_wr_mbox_meat(struct adapter *adap, int mbox, const void *cmd, int size,
		    void *rpl, bool sleep_ok)
{
	static const int delay[] = {
		1, 1, 3, 5, 10, 10, 20, 50, 100, 200
	};

	u32 v;
	u64 res;
	int i, ms, delay_idx;
	const __be64 *p = cmd;
	u32 data_reg = PF_REG(mbox, CIM_PF_MAILBOX_DATA);
	u32 ctl_reg = PF_REG(mbox, CIM_PF_MAILBOX_CTRL);

	if ((size & 15) || size > MBOX_LEN)
		return -EINVAL;

	/*
	 * If the device is off-line, as in EEH, commands will time out.
	 * Fail them early so we don't waste time waiting.
	 */
	if (adap->pdev->error_state != pci_channel_io_normal)
		return -EIO;

	v = MBOWNER_GET(t4_read_reg(adap, ctl_reg));
	for (i = 0; v == MBOX_OWNER_NONE && i < 3; i++)
		v = MBOWNER_GET(t4_read_reg(adap, ctl_reg));

	if (v != MBOX_OWNER_DRV)
		return v ? -EBUSY : -ETIMEDOUT;

	for (i = 0; i < size; i += 8)
		t4_write_reg64(adap, data_reg + i, be64_to_cpu(*p++));

	t4_write_reg(adap, ctl_reg, MBMSGVALID | MBOWNER(MBOX_OWNER_FW));
	t4_read_reg(adap, ctl_reg);          /* flush write */

	delay_idx = 0;
	ms = delay[0];

	for (i = 0; i < FW_CMD_MAX_TIMEOUT; i += ms) {
		if (sleep_ok) {
			ms = delay[delay_idx];  /* last element may repeat */
			if (delay_idx < ARRAY_SIZE(delay) - 1)
				delay_idx++;
			msleep(ms);
		} else
			mdelay(ms);

		v = t4_read_reg(adap, ctl_reg);
		if (MBOWNER_GET(v) == MBOX_OWNER_DRV) {
			if (!(v & MBMSGVALID)) {
				t4_write_reg(adap, ctl_reg, 0);
				continue;
			}

			res = t4_read_reg64(adap, data_reg);
			if (FW_CMD_OP_GET(res >> 32) == FW_DEBUG_CMD) {
				fw_asrt(adap, data_reg);
				res = FW_CMD_RETVAL(EIO);
			} else if (rpl)
				get_mbox_rpl(adap, rpl, size / 8, data_reg);

			if (FW_CMD_RETVAL_GET((int)res))
				dump_mbox(adap, mbox, data_reg);
			t4_write_reg(adap, ctl_reg, 0);
			return -FW_CMD_RETVAL_GET((int)res);
		}
	}

	dump_mbox(adap, mbox, data_reg);
	dev_err(adap->pdev_dev, "command %#x in mailbox %d timed out\n",
		*(const u8 *)cmd, mbox);
	return -ETIMEDOUT;
}

/**
 *	t4_mc_read - read from MC through backdoor accesses
 *	@adap: the adapter
 *	@addr: address of first byte requested
 *	@idx: which MC to access
 *	@data: 64 bytes of data containing the requested address
 *	@ecc: where to store the corresponding 64-bit ECC word
 *
 *	Read 64 bytes of data from MC starting at a 64-byte-aligned address
 *	that covers the requested address @addr.  If @parity is not %NULL it
 *	is assigned the 64-bit ECC word for the read data.
 */
int t4_mc_read(struct adapter *adap, int idx, u32 addr, __be32 *data, u64 *ecc)
{
	int i;
	u32 mc_bist_cmd, mc_bist_cmd_addr, mc_bist_cmd_len;
	u32 mc_bist_status_rdata, mc_bist_data_pattern;

	if (is_t4(adap->params.chip)) {
		mc_bist_cmd = MC_BIST_CMD;
		mc_bist_cmd_addr = MC_BIST_CMD_ADDR;
		mc_bist_cmd_len = MC_BIST_CMD_LEN;
		mc_bist_status_rdata = MC_BIST_STATUS_RDATA;
		mc_bist_data_pattern = MC_BIST_DATA_PATTERN;
	} else {
		mc_bist_cmd = MC_REG(MC_P_BIST_CMD, idx);
		mc_bist_cmd_addr = MC_REG(MC_P_BIST_CMD_ADDR, idx);
		mc_bist_cmd_len = MC_REG(MC_P_BIST_CMD_LEN, idx);
		mc_bist_status_rdata = MC_REG(MC_P_BIST_STATUS_RDATA, idx);
		mc_bist_data_pattern = MC_REG(MC_P_BIST_DATA_PATTERN, idx);
	}

	if (t4_read_reg(adap, mc_bist_cmd) & START_BIST)
		return -EBUSY;
	t4_write_reg(adap, mc_bist_cmd_addr, addr & ~0x3fU);
	t4_write_reg(adap, mc_bist_cmd_len, 64);
	t4_write_reg(adap, mc_bist_data_pattern, 0xc);
	t4_write_reg(adap, mc_bist_cmd, BIST_OPCODE(1) | START_BIST |
		     BIST_CMD_GAP(1));
	i = t4_wait_op_done(adap, mc_bist_cmd, START_BIST, 0, 10, 1);
	if (i)
		return i;

#define MC_DATA(i) MC_BIST_STATUS_REG(mc_bist_status_rdata, i)

	for (i = 15; i >= 0; i--)
		*data++ = htonl(t4_read_reg(adap, MC_DATA(i)));
	if (ecc)
		*ecc = t4_read_reg64(adap, MC_DATA(16));
#undef MC_DATA
	return 0;
}

/**
 *	t4_edc_read - read from EDC through backdoor accesses
 *	@adap: the adapter
 *	@idx: which EDC to access
 *	@addr: address of first byte requested
 *	@data: 64 bytes of data containing the requested address
 *	@ecc: where to store the corresponding 64-bit ECC word
 *
 *	Read 64 bytes of data from EDC starting at a 64-byte-aligned address
 *	that covers the requested address @addr.  If @parity is not %NULL it
 *	is assigned the 64-bit ECC word for the read data.
 */
int t4_edc_read(struct adapter *adap, int idx, u32 addr, __be32 *data, u64 *ecc)
{
	int i;
	u32 edc_bist_cmd, edc_bist_cmd_addr, edc_bist_cmd_len;
	u32 edc_bist_cmd_data_pattern, edc_bist_status_rdata;

	if (is_t4(adap->params.chip)) {
		edc_bist_cmd = EDC_REG(EDC_BIST_CMD, idx);
		edc_bist_cmd_addr = EDC_REG(EDC_BIST_CMD_ADDR, idx);
		edc_bist_cmd_len = EDC_REG(EDC_BIST_CMD_LEN, idx);
		edc_bist_cmd_data_pattern = EDC_REG(EDC_BIST_DATA_PATTERN,
						    idx);
		edc_bist_status_rdata = EDC_REG(EDC_BIST_STATUS_RDATA,
						    idx);
	} else {
		edc_bist_cmd = EDC_REG_T5(EDC_H_BIST_CMD, idx);
		edc_bist_cmd_addr = EDC_REG_T5(EDC_H_BIST_CMD_ADDR, idx);
		edc_bist_cmd_len = EDC_REG_T5(EDC_H_BIST_CMD_LEN, idx);
		edc_bist_cmd_data_pattern =
			EDC_REG_T5(EDC_H_BIST_DATA_PATTERN, idx);
		edc_bist_status_rdata =
			 EDC_REG_T5(EDC_H_BIST_STATUS_RDATA, idx);
	}

	if (t4_read_reg(adap, edc_bist_cmd) & START_BIST)
		return -EBUSY;
	t4_write_reg(adap, edc_bist_cmd_addr, addr & ~0x3fU);
	t4_write_reg(adap, edc_bist_cmd_len, 64);
	t4_write_reg(adap, edc_bist_cmd_data_pattern, 0xc);
	t4_write_reg(adap, edc_bist_cmd,
		     BIST_OPCODE(1) | BIST_CMD_GAP(1) | START_BIST);
	i = t4_wait_op_done(adap, edc_bist_cmd, START_BIST, 0, 10, 1);
	if (i)
		return i;

#define EDC_DATA(i) (EDC_BIST_STATUS_REG(edc_bist_status_rdata, i))

	for (i = 15; i >= 0; i--)
		*data++ = htonl(t4_read_reg(adap, EDC_DATA(i)));
	if (ecc)
		*ecc = t4_read_reg64(adap, EDC_DATA(16));
#undef EDC_DATA
	return 0;
}

/*
 *	t4_mem_win_rw - read/write memory through PCIE memory window
 *	@adap: the adapter
 *	@addr: address of first byte requested
 *	@data: MEMWIN0_APERTURE bytes of data containing the requested address
 *	@dir: direction of transfer 1 => read, 0 => write
 *
 *	Read/write MEMWIN0_APERTURE bytes of data from MC starting at a
 *	MEMWIN0_APERTURE-byte-aligned address that covers the requested
 *	address @addr.
 */
static int t4_mem_win_rw(struct adapter *adap, u32 addr, __be32 *data, int dir)
{
	int i;
	u32 win_pf = is_t4(adap->params.chip) ? 0 : V_PFNUM(adap->fn);

	/*
	 * Setup offset into PCIE memory window.  Address must be a
	 * MEMWIN0_APERTURE-byte-aligned address.  (Read back MA register to
	 * ensure that changes propagate before we attempt to use the new
	 * values.)
	 */
	t4_write_reg(adap, PCIE_MEM_ACCESS_OFFSET,
		     (addr & ~(MEMWIN0_APERTURE - 1)) | win_pf);
	t4_read_reg(adap, PCIE_MEM_ACCESS_OFFSET);

	/* Collecting data 4 bytes at a time upto MEMWIN0_APERTURE */
	for (i = 0; i < MEMWIN0_APERTURE; i = i+0x4) {
		if (dir)
			*data++ = (__force __be32) t4_read_reg(adap,
							(MEMWIN0_BASE + i));
		else
			t4_write_reg(adap, (MEMWIN0_BASE + i),
				     (__force u32) *data++);
	}

	return 0;
}

/**
 *	t4_memory_rw - read/write EDC 0, EDC 1 or MC via PCIE memory window
 *	@adap: the adapter
 *	@mtype: memory type: MEM_EDC0, MEM_EDC1 or MEM_MC
 *	@addr: address within indicated memory type
 *	@len: amount of memory to transfer
 *	@buf: host memory buffer
 *	@dir: direction of transfer 1 => read, 0 => write
 *
 *	Reads/writes an [almost] arbitrary memory region in the firmware: the
 *	firmware memory address, length and host buffer must be aligned on
 *	32-bit boudaries.  The memory is transferred as a raw byte sequence
 *	from/to the firmware's memory.  If this memory contains data
 *	structures which contain multi-byte integers, it's the callers
 *	responsibility to perform appropriate byte order conversions.
 */
static int t4_memory_rw(struct adapter *adap, int mtype, u32 addr, u32 len,
			__be32 *buf, int dir)
{
	u32 pos, start, end, offset, memoffset;
	u32 edc_size, mc_size;
	int ret = 0;
	__be32 *data;

	/*
	 * Argument sanity checks ...
	 */
	if ((addr & 0x3) || (len & 0x3))
		return -EINVAL;

	data = vmalloc(MEMWIN0_APERTURE);
	if (!data)
		return -ENOMEM;

	/* Offset into the region of memory which is being accessed
	 * MEM_EDC0 = 0
	 * MEM_EDC1 = 1
	 * MEM_MC   = 2 -- T4
	 * MEM_MC0  = 2 -- For T5
	 * MEM_MC1  = 3 -- For T5
	 */
	edc_size  = EDRAM_SIZE_GET(t4_read_reg(adap, MA_EDRAM0_BAR));
	if (mtype != MEM_MC1)
		memoffset = (mtype * (edc_size * 1024 * 1024));
	else {
		mc_size = EXT_MEM_SIZE_GET(t4_read_reg(adap,
						       MA_EXT_MEMORY_BAR));
		memoffset = (MEM_MC0 * edc_size + mc_size) * 1024 * 1024;
	}

	/* Determine the PCIE_MEM_ACCESS_OFFSET */
	addr = addr + memoffset;

	/*
	 * The underlaying EDC/MC read routines read MEMWIN0_APERTURE bytes
	 * at a time so we need to round down the start and round up the end.
	 * We'll start copying out of the first line at (addr - start) a word
	 * at a time.
	 */
	start = addr & ~(MEMWIN0_APERTURE-1);
	end = (addr + len + MEMWIN0_APERTURE-1) & ~(MEMWIN0_APERTURE-1);
	offset = (addr - start)/sizeof(__be32);

	for (pos = start; pos < end; pos += MEMWIN0_APERTURE, offset = 0) {

		/*
		 * If we're writing, copy the data from the caller's memory
		 * buffer
		 */
		if (!dir) {
			/*
			 * If we're doing a partial write, then we need to do
			 * a read-modify-write ...
			 */
			if (offset || len < MEMWIN0_APERTURE) {
				ret = t4_mem_win_rw(adap, pos, data, 1);
				if (ret)
					break;
			}
			while (offset < (MEMWIN0_APERTURE/sizeof(__be32)) &&
			       len > 0) {
				data[offset++] = *buf++;
				len -= sizeof(__be32);
			}
		}

		/*
		 * Transfer a block of memory and bail if there's an error.
		 */
		ret = t4_mem_win_rw(adap, pos, data, dir);
		if (ret)
			break;

		/*
		 * If we're reading, copy the data into the caller's memory
		 * buffer.
		 */
		if (dir)
			while (offset < (MEMWIN0_APERTURE/sizeof(__be32)) &&
			       len > 0) {
				*buf++ = data[offset++];
				len -= sizeof(__be32);
			}
	}

	vfree(data);
	return ret;
}

int t4_memory_write(struct adapter *adap, int mtype, u32 addr, u32 len,
		    __be32 *buf)
{
	return t4_memory_rw(adap, mtype, addr, len, buf, 0);
}

#define EEPROM_STAT_ADDR   0x7bfc
#define VPD_BASE           0x400
#define VPD_BASE_OLD       0
#define VPD_LEN            1024

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
 *	get_vpd_params - read VPD parameters from VPD EEPROM
 *	@adapter: adapter to read
 *	@p: where to store the parameters
 *
 *	Reads card parameters stored in VPD EEPROM.
 */
int get_vpd_params(struct adapter *adapter, struct vpd_params *p)
{
	u32 cclk_param, cclk_val;
	int i, ret, addr;
	int ec, sn, pn;
	u8 *vpd, csum;
	unsigned int vpdr_len, kw_offset, id_len;

	vpd = vmalloc(VPD_LEN);
	if (!vpd)
		return -ENOMEM;

	ret = pci_read_vpd(adapter->pdev, VPD_BASE, sizeof(u32), vpd);
	if (ret < 0)
		goto out;
	addr = *vpd == 0x82 ? VPD_BASE : VPD_BASE_OLD;

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
#undef FIND_VPD_KW

	memcpy(p->id, vpd + PCI_VPD_LRDT_TAG_SIZE, id_len);
	strim(p->id);
	memcpy(p->ec, vpd + ec, EC_LEN);
	strim(p->ec);
	i = pci_vpd_info_field_size(vpd + sn - PCI_VPD_INFO_FLD_HDR_SIZE);
	memcpy(p->sn, vpd + sn, min(i, SERNUM_LEN));
	strim(p->sn);
	memcpy(p->pn, vpd + pn, min(i, PN_LEN));
	strim(p->pn);

	/*
	 * Ask firmware for the Core Clock since it knows how to translate the
	 * Reference Clock ('V2') VPD field into a Core Clock value ...
	 */
	cclk_param = (FW_PARAMS_MNEM(FW_PARAMS_MNEM_DEV) |
		      FW_PARAMS_PARAM_X(FW_PARAMS_PARAM_DEV_CCLK));
	ret = t4_query_params(adapter, adapter->mbox, 0, 0,
			      1, &cclk_param, &cclk_val);

out:
	vfree(vpd);
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
	if (t4_read_reg(adapter, SF_OP) & SF_BUSY)
		return -EBUSY;
	cont = cont ? SF_CONT : 0;
	lock = lock ? SF_LOCK : 0;
	t4_write_reg(adapter, SF_OP, lock | cont | BYTECNT(byte_cnt - 1));
	ret = t4_wait_op_done(adapter, SF_OP, SF_BUSY, 0, SF_ATTEMPTS, 5);
	if (!ret)
		*valp = t4_read_reg(adapter, SF_DATA);
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
	if (t4_read_reg(adapter, SF_OP) & SF_BUSY)
		return -EBUSY;
	cont = cont ? SF_CONT : 0;
	lock = lock ? SF_LOCK : 0;
	t4_write_reg(adapter, SF_DATA, val);
	t4_write_reg(adapter, SF_OP, lock |
		     cont | BYTECNT(byte_cnt - 1) | OP_WR);
	return t4_wait_op_done(adapter, SF_OP, SF_BUSY, 0, SF_ATTEMPTS, 5);
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
 *	natural endianess.
 */
static int t4_read_flash(struct adapter *adapter, unsigned int addr,
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
			t4_write_reg(adapter, SF_OP, 0);    /* unlock SF */
		if (ret)
			return ret;
		if (byte_oriented)
			*data = (__force __u32) (htonl(*data));
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

	t4_write_reg(adapter, SF_OP, 0);    /* unlock SF */

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
	t4_write_reg(adapter, SF_OP, 0);    /* unlock SF */
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
		FW_HDR_FW_VER_MAJOR_GET(c), FW_HDR_FW_VER_MINOR_GET(c),
		FW_HDR_FW_VER_MICRO_GET(c), FW_HDR_FW_VER_BUILD_GET(c), reason,
		FW_HDR_FW_VER_MAJOR_GET(k), FW_HDR_FW_VER_MINOR_GET(k),
		FW_HDR_FW_VER_MICRO_GET(k), FW_HDR_FW_VER_BUILD_GET(k));

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
		memcpy(card_fw, fs_fw, sizeof(*card_fw));
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
			FW_HDR_FW_VER_MAJOR_GET(d), FW_HDR_FW_VER_MINOR_GET(d),
			FW_HDR_FW_VER_MICRO_GET(d), FW_HDR_FW_VER_BUILD_GET(d),
			FW_HDR_FW_VER_MAJOR_GET(c), FW_HDR_FW_VER_MINOR_GET(c),
			FW_HDR_FW_VER_MICRO_GET(c), FW_HDR_FW_VER_BUILD_GET(c),
			FW_HDR_FW_VER_MAJOR_GET(k), FW_HDR_FW_VER_MINOR_GET(k),
			FW_HDR_FW_VER_MICRO_GET(k), FW_HDR_FW_VER_BUILD_GET(k));
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
	t4_write_reg(adapter, SF_OP, 0);    /* unlock SF */
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
	if (ntohs(hdr->len512) * 512 != size) {
		dev_err(adap->pdev_dev,
			"FW image size differs from size in FW header\n");
		return -EINVAL;
	}
	if (size > FW_MAX_SIZE) {
		dev_err(adap->pdev_dev, "FW image too large, max is %u bytes\n",
			FW_MAX_SIZE);
		return -EFBIG;
	}

	for (csum = 0, i = 0; i < size / sizeof(csum); i++)
		csum += ntohl(p[i]);

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
	((struct fw_hdr *)first_page)->fw_ver = htonl(0xffffffff);
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
	return ret;
}

#define ADVERT_MASK (FW_PORT_CAP_SPEED_100M | FW_PORT_CAP_SPEED_1G |\
		     FW_PORT_CAP_SPEED_10G | FW_PORT_CAP_SPEED_40G | \
		     FW_PORT_CAP_ANEG)

/**
 *	t4_link_start - apply link configuration to MAC/PHY
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
int t4_link_start(struct adapter *adap, unsigned int mbox, unsigned int port,
		  struct link_config *lc)
{
	struct fw_port_cmd c;
	unsigned int fc = 0, mdi = FW_PORT_MDI(FW_PORT_MDI_AUTO);

	lc->link_ok = 0;
	if (lc->requested_fc & PAUSE_RX)
		fc |= FW_PORT_CAP_FC_RX;
	if (lc->requested_fc & PAUSE_TX)
		fc |= FW_PORT_CAP_FC_TX;

	memset(&c, 0, sizeof(c));
	c.op_to_portid = htonl(FW_CMD_OP(FW_PORT_CMD) | FW_CMD_REQUEST |
			       FW_CMD_EXEC | FW_PORT_CMD_PORTID(port));
	c.action_to_len16 = htonl(FW_PORT_CMD_ACTION(FW_PORT_ACTION_L1_CFG) |
				  FW_LEN16(c));

	if (!(lc->supported & FW_PORT_CAP_ANEG)) {
		c.u.l1cfg.rcap = htonl((lc->supported & ADVERT_MASK) | fc);
		lc->fc = lc->requested_fc & (PAUSE_RX | PAUSE_TX);
	} else if (lc->autoneg == AUTONEG_DISABLE) {
		c.u.l1cfg.rcap = htonl(lc->requested_speed | fc | mdi);
		lc->fc = lc->requested_fc & (PAUSE_RX | PAUSE_TX);
	} else
		c.u.l1cfg.rcap = htonl(lc->advertising | fc | mdi);

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
	c.op_to_portid = htonl(FW_CMD_OP(FW_PORT_CMD) | FW_CMD_REQUEST |
			       FW_CMD_EXEC | FW_PORT_CMD_PORTID(port));
	c.action_to_len16 = htonl(FW_PORT_CMD_ACTION(FW_PORT_ACTION_L1_CFG) |
				  FW_LEN16(c));
	c.u.l1cfg.rcap = htonl(FW_PORT_CAP_ANEG);
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
		{ RNPP, "RXNP array parity error", -1, 1 },
		{ RPCP, "RXPC array parity error", -1, 1 },
		{ RCIP, "RXCIF array parity error", -1, 1 },
		{ RCCP, "Rx completions control array parity error", -1, 1 },
		{ RFTP, "RXFT array parity error", -1, 1 },
		{ 0 }
	};
	static const struct intr_info pcie_port_intr_info[] = {
		{ TPCP, "TXPC array parity error", -1, 1 },
		{ TNPP, "TXNP array parity error", -1, 1 },
		{ TFTP, "TXFT array parity error", -1, 1 },
		{ TCAP, "TXCA array parity error", -1, 1 },
		{ TCIP, "TXCIF array parity error", -1, 1 },
		{ RCAP, "RXCA array parity error", -1, 1 },
		{ OTDD, "outbound request TLP discarded", -1, 1 },
		{ RDPE, "Rx data parity error", -1, 1 },
		{ TDUE, "Tx uncorrectable data error", -1, 1 },
		{ 0 }
	};
	static const struct intr_info pcie_intr_info[] = {
		{ MSIADDRLPERR, "MSI AddrL parity error", -1, 1 },
		{ MSIADDRHPERR, "MSI AddrH parity error", -1, 1 },
		{ MSIDATAPERR, "MSI data parity error", -1, 1 },
		{ MSIXADDRLPERR, "MSI-X AddrL parity error", -1, 1 },
		{ MSIXADDRHPERR, "MSI-X AddrH parity error", -1, 1 },
		{ MSIXDATAPERR, "MSI-X data parity error", -1, 1 },
		{ MSIXDIPERR, "MSI-X DI parity error", -1, 1 },
		{ PIOCPLPERR, "PCI PIO completion FIFO parity error", -1, 1 },
		{ PIOREQPERR, "PCI PIO request FIFO parity error", -1, 1 },
		{ TARTAGPERR, "PCI PCI target tag FIFO parity error", -1, 1 },
		{ CCNTPERR, "PCI CMD channel count parity error", -1, 1 },
		{ CREQPERR, "PCI CMD channel request parity error", -1, 1 },
		{ CRSPPERR, "PCI CMD channel response parity error", -1, 1 },
		{ DCNTPERR, "PCI DMA channel count parity error", -1, 1 },
		{ DREQPERR, "PCI DMA channel request parity error", -1, 1 },
		{ DRSPPERR, "PCI DMA channel response parity error", -1, 1 },
		{ HCNTPERR, "PCI HMA channel count parity error", -1, 1 },
		{ HREQPERR, "PCI HMA channel request parity error", -1, 1 },
		{ HRSPPERR, "PCI HMA channel response parity error", -1, 1 },
		{ CFGSNPPERR, "PCI config snoop FIFO parity error", -1, 1 },
		{ FIDPERR, "PCI FID parity error", -1, 1 },
		{ INTXCLRPERR, "PCI INTx clear parity error", -1, 1 },
		{ MATAGPERR, "PCI MA tag parity error", -1, 1 },
		{ PIOTAGPERR, "PCI PIO tag parity error", -1, 1 },
		{ RXCPLPERR, "PCI Rx completion parity error", -1, 1 },
		{ RXWRPERR, "PCI Rx write parity error", -1, 1 },
		{ RPLPERR, "PCI replay buffer parity error", -1, 1 },
		{ PCIESINT, "PCI core secondary fault", -1, 1 },
		{ PCIEPINT, "PCI core primary fault", -1, 1 },
		{ UNXSPLCPLERR, "PCI unexpected split completion error", -1, 0 },
		{ 0 }
	};

	static struct intr_info t5_pcie_intr_info[] = {
		{ MSTGRPPERR, "Master Response Read Queue parity error",
		  -1, 1 },
		{ MSTTIMEOUTPERR, "Master Timeout FIFO parity error", -1, 1 },
		{ MSIXSTIPERR, "MSI-X STI SRAM parity error", -1, 1 },
		{ MSIXADDRLPERR, "MSI-X AddrL parity error", -1, 1 },
		{ MSIXADDRHPERR, "MSI-X AddrH parity error", -1, 1 },
		{ MSIXDATAPERR, "MSI-X data parity error", -1, 1 },
		{ MSIXDIPERR, "MSI-X DI parity error", -1, 1 },
		{ PIOCPLGRPPERR, "PCI PIO completion Group FIFO parity error",
		  -1, 1 },
		{ PIOREQGRPPERR, "PCI PIO request Group FIFO parity error",
		  -1, 1 },
		{ TARTAGPERR, "PCI PCI target tag FIFO parity error", -1, 1 },
		{ MSTTAGQPERR, "PCI master tag queue parity error", -1, 1 },
		{ CREQPERR, "PCI CMD channel request parity error", -1, 1 },
		{ CRSPPERR, "PCI CMD channel response parity error", -1, 1 },
		{ DREQWRPERR, "PCI DMA channel write request parity error",
		  -1, 1 },
		{ DREQPERR, "PCI DMA channel request parity error", -1, 1 },
		{ DRSPPERR, "PCI DMA channel response parity error", -1, 1 },
		{ HREQWRPERR, "PCI HMA channel count parity error", -1, 1 },
		{ HREQPERR, "PCI HMA channel request parity error", -1, 1 },
		{ HRSPPERR, "PCI HMA channel response parity error", -1, 1 },
		{ CFGSNPPERR, "PCI config snoop FIFO parity error", -1, 1 },
		{ FIDPERR, "PCI FID parity error", -1, 1 },
		{ VFIDPERR, "PCI INTx clear parity error", -1, 1 },
		{ MAGRPPERR, "PCI MA group FIFO parity error", -1, 1 },
		{ PIOTAGPERR, "PCI PIO tag parity error", -1, 1 },
		{ IPRXHDRGRPPERR, "PCI IP Rx header group parity error",
		  -1, 1 },
		{ IPRXDATAGRPPERR, "PCI IP Rx data group parity error", -1, 1 },
		{ RPLPERR, "PCI IP replay buffer parity error", -1, 1 },
		{ IPSOTPERR, "PCI IP SOT buffer parity error", -1, 1 },
		{ TRGT1GRPPERR, "PCI TRGT1 group FIFOs parity error", -1, 1 },
		{ READRSPERR, "Outbound read error", -1, 0 },
		{ 0 }
	};

	int fat;

	fat = t4_handle_intr_status(adapter,
				    PCIE_CORE_UTL_SYSTEM_BUS_AGENT_STATUS,
				    sysbus_intr_info) +
	      t4_handle_intr_status(adapter,
				    PCIE_CORE_UTL_PCI_EXPRESS_PORT_STATUS,
				    pcie_port_intr_info) +
	      t4_handle_intr_status(adapter, PCIE_INT_CAUSE,
				    is_t4(adapter->params.chip) ?
				    pcie_intr_info : t5_pcie_intr_info);

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
		{ FLMTXFLSTEMPTY, "TP out of Tx pages", -1, 1 },
		{ 0 }
	};

	if (t4_handle_intr_status(adapter, TP_INT_CAUSE, tp_intr_info))
		t4_fatal_err(adapter);
}

/*
 * SGE interrupt handler.
 */
static void sge_intr_handler(struct adapter *adapter)
{
	u64 v;

	static const struct intr_info sge_intr_info[] = {
		{ ERR_CPL_EXCEED_IQE_SIZE,
		  "SGE received CPL exceeding IQE size", -1, 1 },
		{ ERR_INVALID_CIDX_INC,
		  "SGE GTS CIDX increment too large", -1, 0 },
		{ ERR_CPL_OPCODE_0, "SGE received 0-length CPL", -1, 0 },
		{ DBFIFO_LP_INT, NULL, -1, 0, t4_db_full },
		{ DBFIFO_HP_INT, NULL, -1, 0, t4_db_full },
		{ ERR_DROPPED_DB, NULL, -1, 0, t4_db_dropped },
		{ ERR_DATA_CPL_ON_HIGH_QID1 | ERR_DATA_CPL_ON_HIGH_QID0,
		  "SGE IQID > 1023 received CPL for FL", -1, 0 },
		{ ERR_BAD_DB_PIDX3, "SGE DBP 3 pidx increment too large", -1,
		  0 },
		{ ERR_BAD_DB_PIDX2, "SGE DBP 2 pidx increment too large", -1,
		  0 },
		{ ERR_BAD_DB_PIDX1, "SGE DBP 1 pidx increment too large", -1,
		  0 },
		{ ERR_BAD_DB_PIDX0, "SGE DBP 0 pidx increment too large", -1,
		  0 },
		{ ERR_ING_CTXT_PRIO,
		  "SGE too many priority ingress contexts", -1, 0 },
		{ ERR_EGR_CTXT_PRIO,
		  "SGE too many priority egress contexts", -1, 0 },
		{ INGRESS_SIZE_ERR, "SGE illegal ingress QID", -1, 0 },
		{ EGRESS_SIZE_ERR, "SGE illegal egress QID", -1, 0 },
		{ 0 }
	};

	v = (u64)t4_read_reg(adapter, SGE_INT_CAUSE1) |
		((u64)t4_read_reg(adapter, SGE_INT_CAUSE2) << 32);
	if (v) {
		dev_alert(adapter->pdev_dev, "SGE parity error (%#llx)\n",
				(unsigned long long)v);
		t4_write_reg(adapter, SGE_INT_CAUSE1, v);
		t4_write_reg(adapter, SGE_INT_CAUSE2, v >> 32);
	}

	if (t4_handle_intr_status(adapter, SGE_INT_CAUSE3, sge_intr_info) ||
	    v != 0)
		t4_fatal_err(adapter);
}

/*
 * CIM interrupt handler.
 */
static void cim_intr_handler(struct adapter *adapter)
{
	static const struct intr_info cim_intr_info[] = {
		{ PREFDROPINT, "CIM control register prefetch drop", -1, 1 },
		{ OBQPARERR, "CIM OBQ parity error", -1, 1 },
		{ IBQPARERR, "CIM IBQ parity error", -1, 1 },
		{ MBUPPARERR, "CIM mailbox uP parity error", -1, 1 },
		{ MBHOSTPARERR, "CIM mailbox host parity error", -1, 1 },
		{ TIEQINPARERRINT, "CIM TIEQ outgoing parity error", -1, 1 },
		{ TIEQOUTPARERRINT, "CIM TIEQ incoming parity error", -1, 1 },
		{ 0 }
	};
	static const struct intr_info cim_upintr_info[] = {
		{ RSVDSPACEINT, "CIM reserved space access", -1, 1 },
		{ ILLTRANSINT, "CIM illegal transaction", -1, 1 },
		{ ILLWRINT, "CIM illegal write", -1, 1 },
		{ ILLRDINT, "CIM illegal read", -1, 1 },
		{ ILLRDBEINT, "CIM illegal read BE", -1, 1 },
		{ ILLWRBEINT, "CIM illegal write BE", -1, 1 },
		{ SGLRDBOOTINT, "CIM single read from boot space", -1, 1 },
		{ SGLWRBOOTINT, "CIM single write to boot space", -1, 1 },
		{ BLKWRBOOTINT, "CIM block write to boot space", -1, 1 },
		{ SGLRDFLASHINT, "CIM single read from flash space", -1, 1 },
		{ SGLWRFLASHINT, "CIM single write to flash space", -1, 1 },
		{ BLKWRFLASHINT, "CIM block write to flash space", -1, 1 },
		{ SGLRDEEPROMINT, "CIM single EEPROM read", -1, 1 },
		{ SGLWREEPROMINT, "CIM single EEPROM write", -1, 1 },
		{ BLKRDEEPROMINT, "CIM block EEPROM read", -1, 1 },
		{ BLKWREEPROMINT, "CIM block EEPROM write", -1, 1 },
		{ SGLRDCTLINT , "CIM single read from CTL space", -1, 1 },
		{ SGLWRCTLINT , "CIM single write to CTL space", -1, 1 },
		{ BLKRDCTLINT , "CIM block read from CTL space", -1, 1 },
		{ BLKWRCTLINT , "CIM block write to CTL space", -1, 1 },
		{ SGLRDPLINT , "CIM single read from PL space", -1, 1 },
		{ SGLWRPLINT , "CIM single write to PL space", -1, 1 },
		{ BLKRDPLINT , "CIM block read from PL space", -1, 1 },
		{ BLKWRPLINT , "CIM block write to PL space", -1, 1 },
		{ REQOVRLOOKUPINT , "CIM request FIFO overwrite", -1, 1 },
		{ RSPOVRLOOKUPINT , "CIM response FIFO overwrite", -1, 1 },
		{ TIMEOUTINT , "CIM PIF timeout", -1, 1 },
		{ TIMEOUTMAINT , "CIM PIF MA timeout", -1, 1 },
		{ 0 }
	};

	int fat;

	fat = t4_handle_intr_status(adapter, CIM_HOST_INT_CAUSE,
				    cim_intr_info) +
	      t4_handle_intr_status(adapter, CIM_HOST_UPACC_INT_CAUSE,
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

	if (t4_handle_intr_status(adapter, ULP_RX_INT_CAUSE, ulprx_intr_info))
		t4_fatal_err(adapter);
}

/*
 * ULP TX interrupt handler.
 */
static void ulptx_intr_handler(struct adapter *adapter)
{
	static const struct intr_info ulptx_intr_info[] = {
		{ PBL_BOUND_ERR_CH3, "ULPTX channel 3 PBL out of bounds", -1,
		  0 },
		{ PBL_BOUND_ERR_CH2, "ULPTX channel 2 PBL out of bounds", -1,
		  0 },
		{ PBL_BOUND_ERR_CH1, "ULPTX channel 1 PBL out of bounds", -1,
		  0 },
		{ PBL_BOUND_ERR_CH0, "ULPTX channel 0 PBL out of bounds", -1,
		  0 },
		{ 0xfffffff, "ULPTX parity error", -1, 1 },
		{ 0 }
	};

	if (t4_handle_intr_status(adapter, ULP_TX_INT_CAUSE, ulptx_intr_info))
		t4_fatal_err(adapter);
}

/*
 * PM TX interrupt handler.
 */
static void pmtx_intr_handler(struct adapter *adapter)
{
	static const struct intr_info pmtx_intr_info[] = {
		{ PCMD_LEN_OVFL0, "PMTX channel 0 pcmd too large", -1, 1 },
		{ PCMD_LEN_OVFL1, "PMTX channel 1 pcmd too large", -1, 1 },
		{ PCMD_LEN_OVFL2, "PMTX channel 2 pcmd too large", -1, 1 },
		{ ZERO_C_CMD_ERROR, "PMTX 0-length pcmd", -1, 1 },
		{ PMTX_FRAMING_ERROR, "PMTX framing error", -1, 1 },
		{ OESPI_PAR_ERROR, "PMTX oespi parity error", -1, 1 },
		{ DB_OPTIONS_PAR_ERROR, "PMTX db_options parity error", -1, 1 },
		{ ICSPI_PAR_ERROR, "PMTX icspi parity error", -1, 1 },
		{ C_PCMD_PAR_ERROR, "PMTX c_pcmd parity error", -1, 1},
		{ 0 }
	};

	if (t4_handle_intr_status(adapter, PM_TX_INT_CAUSE, pmtx_intr_info))
		t4_fatal_err(adapter);
}

/*
 * PM RX interrupt handler.
 */
static void pmrx_intr_handler(struct adapter *adapter)
{
	static const struct intr_info pmrx_intr_info[] = {
		{ ZERO_E_CMD_ERROR, "PMRX 0-length pcmd", -1, 1 },
		{ PMRX_FRAMING_ERROR, "PMRX framing error", -1, 1 },
		{ OCSPI_PAR_ERROR, "PMRX ocspi parity error", -1, 1 },
		{ DB_OPTIONS_PAR_ERROR, "PMRX db_options parity error", -1, 1 },
		{ IESPI_PAR_ERROR, "PMRX iespi parity error", -1, 1 },
		{ E_PCMD_PAR_ERROR, "PMRX e_pcmd parity error", -1, 1},
		{ 0 }
	};

	if (t4_handle_intr_status(adapter, PM_RX_INT_CAUSE, pmrx_intr_info))
		t4_fatal_err(adapter);
}

/*
 * CPL switch interrupt handler.
 */
static void cplsw_intr_handler(struct adapter *adapter)
{
	static const struct intr_info cplsw_intr_info[] = {
		{ CIM_OP_MAP_PERR, "CPLSW CIM op_map parity error", -1, 1 },
		{ CIM_OVFL_ERROR, "CPLSW CIM overflow", -1, 1 },
		{ TP_FRAMING_ERROR, "CPLSW TP framing error", -1, 1 },
		{ SGE_FRAMING_ERROR, "CPLSW SGE framing error", -1, 1 },
		{ CIM_FRAMING_ERROR, "CPLSW CIM framing error", -1, 1 },
		{ ZERO_SWITCH_ERROR, "CPLSW no-switch error", -1, 1 },
		{ 0 }
	};

	if (t4_handle_intr_status(adapter, CPL_INTR_CAUSE, cplsw_intr_info))
		t4_fatal_err(adapter);
}

/*
 * LE interrupt handler.
 */
static void le_intr_handler(struct adapter *adap)
{
	static const struct intr_info le_intr_info[] = {
		{ LIPMISS, "LE LIP miss", -1, 0 },
		{ LIP0, "LE 0 LIP error", -1, 0 },
		{ PARITYERR, "LE parity error", -1, 1 },
		{ UNKNOWNCMD, "LE unknown command", -1, 1 },
		{ REQQPARERR, "LE request queue parity error", -1, 1 },
		{ 0 }
	};

	if (t4_handle_intr_status(adap, LE_DB_INT_CAUSE, le_intr_info))
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
		{ TPFIFO, "MPS Tx TP FIFO parity error", -1, 1 },
		{ NCSIFIFO, "MPS Tx NC-SI FIFO parity error", -1, 1 },
		{ TXDATAFIFO, "MPS Tx data FIFO parity error", -1, 1 },
		{ TXDESCFIFO, "MPS Tx desc FIFO parity error", -1, 1 },
		{ BUBBLE, "MPS Tx underflow", -1, 1 },
		{ SECNTERR, "MPS Tx SOP/EOP error", -1, 1 },
		{ FRMERR, "MPS Tx framing error", -1, 1 },
		{ 0 }
	};
	static const struct intr_info mps_trc_intr_info[] = {
		{ FILTMEM, "MPS TRC filter parity error", -1, 1 },
		{ PKTFIFO, "MPS TRC packet FIFO parity error", -1, 1 },
		{ MISCPERR, "MPS TRC misc parity error", -1, 1 },
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
		{ MATCHSRAM, "MPS match SRAM parity error", -1, 1 },
		{ MATCHTCAM, "MPS match TCAM parity error", -1, 1 },
		{ HASHSRAM, "MPS hash SRAM parity error", -1, 1 },
		{ 0 }
	};

	int fat;

	fat = t4_handle_intr_status(adapter, MPS_RX_PERR_INT_CAUSE,
				    mps_rx_intr_info) +
	      t4_handle_intr_status(adapter, MPS_TX_INT_CAUSE,
				    mps_tx_intr_info) +
	      t4_handle_intr_status(adapter, MPS_TRC_INT_CAUSE,
				    mps_trc_intr_info) +
	      t4_handle_intr_status(adapter, MPS_STAT_PERR_INT_CAUSE_SRAM,
				    mps_stat_sram_intr_info) +
	      t4_handle_intr_status(adapter, MPS_STAT_PERR_INT_CAUSE_TX_FIFO,
				    mps_stat_tx_intr_info) +
	      t4_handle_intr_status(adapter, MPS_STAT_PERR_INT_CAUSE_RX_FIFO,
				    mps_stat_rx_intr_info) +
	      t4_handle_intr_status(adapter, MPS_CLS_INT_CAUSE,
				    mps_cls_intr_info);

	t4_write_reg(adapter, MPS_INT_CAUSE, CLSINT | TRCINT |
		     RXINT | TXINT | STATINT);
	t4_read_reg(adapter, MPS_INT_CAUSE);                    /* flush */
	if (fat)
		t4_fatal_err(adapter);
}

#define MEM_INT_MASK (PERR_INT_CAUSE | ECC_CE_INT_CAUSE | ECC_UE_INT_CAUSE)

/*
 * EDC/MC interrupt handler.
 */
static void mem_intr_handler(struct adapter *adapter, int idx)
{
	static const char name[3][5] = { "EDC0", "EDC1", "MC" };

	unsigned int addr, cnt_addr, v;

	if (idx <= MEM_EDC1) {
		addr = EDC_REG(EDC_INT_CAUSE, idx);
		cnt_addr = EDC_REG(EDC_ECC_STATUS, idx);
	} else {
		addr = MC_INT_CAUSE;
		cnt_addr = MC_ECC_STATUS;
	}

	v = t4_read_reg(adapter, addr) & MEM_INT_MASK;
	if (v & PERR_INT_CAUSE)
		dev_alert(adapter->pdev_dev, "%s FIFO parity error\n",
			  name[idx]);
	if (v & ECC_CE_INT_CAUSE) {
		u32 cnt = ECC_CECNT_GET(t4_read_reg(adapter, cnt_addr));

		t4_write_reg(adapter, cnt_addr, ECC_CECNT_MASK);
		if (printk_ratelimit())
			dev_warn(adapter->pdev_dev,
				 "%u %s correctable ECC data error%s\n",
				 cnt, name[idx], cnt > 1 ? "s" : "");
	}
	if (v & ECC_UE_INT_CAUSE)
		dev_alert(adapter->pdev_dev,
			  "%s uncorrectable ECC data error\n", name[idx]);

	t4_write_reg(adapter, addr, v);
	if (v & (PERR_INT_CAUSE | ECC_UE_INT_CAUSE))
		t4_fatal_err(adapter);
}

/*
 * MA interrupt handler.
 */
static void ma_intr_handler(struct adapter *adap)
{
	u32 v, status = t4_read_reg(adap, MA_INT_CAUSE);

	if (status & MEM_PERR_INT_CAUSE)
		dev_alert(adap->pdev_dev,
			  "MA parity error, parity status %#x\n",
			  t4_read_reg(adap, MA_PARITY_ERROR_STATUS));
	if (status & MEM_WRAP_INT_CAUSE) {
		v = t4_read_reg(adap, MA_INT_WRAP_STATUS);
		dev_alert(adap->pdev_dev, "MA address wrap-around error by "
			  "client %u to address %#x\n",
			  MEM_WRAP_CLIENT_NUM_GET(v),
			  MEM_WRAP_ADDRESS_GET(v) << 4);
	}
	t4_write_reg(adap, MA_INT_CAUSE, status);
	t4_fatal_err(adap);
}

/*
 * SMB interrupt handler.
 */
static void smb_intr_handler(struct adapter *adap)
{
	static const struct intr_info smb_intr_info[] = {
		{ MSTTXFIFOPARINT, "SMB master Tx FIFO parity error", -1, 1 },
		{ MSTRXFIFOPARINT, "SMB master Rx FIFO parity error", -1, 1 },
		{ SLVFIFOPARINT, "SMB slave FIFO parity error", -1, 1 },
		{ 0 }
	};

	if (t4_handle_intr_status(adap, SMB_INT_CAUSE, smb_intr_info))
		t4_fatal_err(adap);
}

/*
 * NC-SI interrupt handler.
 */
static void ncsi_intr_handler(struct adapter *adap)
{
	static const struct intr_info ncsi_intr_info[] = {
		{ CIM_DM_PRTY_ERR, "NC-SI CIM parity error", -1, 1 },
		{ MPS_DM_PRTY_ERR, "NC-SI MPS parity error", -1, 1 },
		{ TXFIFO_PRTY_ERR, "NC-SI Tx FIFO parity error", -1, 1 },
		{ RXFIFO_PRTY_ERR, "NC-SI Rx FIFO parity error", -1, 1 },
		{ 0 }
	};

	if (t4_handle_intr_status(adap, NCSI_INT_CAUSE, ncsi_intr_info))
		t4_fatal_err(adap);
}

/*
 * XGMAC interrupt handler.
 */
static void xgmac_intr_handler(struct adapter *adap, int port)
{
	u32 v, int_cause_reg;

	if (is_t4(adap->params.chip))
		int_cause_reg = PORT_REG(port, XGMAC_PORT_INT_CAUSE);
	else
		int_cause_reg = T5_PORT_REG(port, MAC_PORT_INT_CAUSE);

	v = t4_read_reg(adap, int_cause_reg);

	v &= TXFIFO_PRTY_ERR | RXFIFO_PRTY_ERR;
	if (!v)
		return;

	if (v & TXFIFO_PRTY_ERR)
		dev_alert(adap->pdev_dev, "XGMAC %d Tx FIFO parity error\n",
			  port);
	if (v & RXFIFO_PRTY_ERR)
		dev_alert(adap->pdev_dev, "XGMAC %d Rx FIFO parity error\n",
			  port);
	t4_write_reg(adap, PORT_REG(port, XGMAC_PORT_INT_CAUSE), v);
	t4_fatal_err(adap);
}

/*
 * PL interrupt handler.
 */
static void pl_intr_handler(struct adapter *adap)
{
	static const struct intr_info pl_intr_info[] = {
		{ FATALPERR, "T4 fatal parity error", -1, 1 },
		{ PERRVFID, "PL VFID_MAP parity error", -1, 1 },
		{ 0 }
	};

	if (t4_handle_intr_status(adap, PL_PL_INT_CAUSE, pl_intr_info))
		t4_fatal_err(adap);
}

#define PF_INTR_MASK (PFSW)
#define GLBL_INTR_MASK (CIM | MPS | PL | PCIE | MC | EDC0 | \
		EDC1 | LE | TP | MA | PM_TX | PM_RX | ULP_RX | \
		CPL_SWITCH | SGE | ULP_TX)

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
	u32 cause = t4_read_reg(adapter, PL_INT_CAUSE);

	if (!(cause & GLBL_INTR_MASK))
		return 0;
	if (cause & CIM)
		cim_intr_handler(adapter);
	if (cause & MPS)
		mps_intr_handler(adapter);
	if (cause & NCSI)
		ncsi_intr_handler(adapter);
	if (cause & PL)
		pl_intr_handler(adapter);
	if (cause & SMB)
		smb_intr_handler(adapter);
	if (cause & XGMAC0)
		xgmac_intr_handler(adapter, 0);
	if (cause & XGMAC1)
		xgmac_intr_handler(adapter, 1);
	if (cause & XGMAC_KR0)
		xgmac_intr_handler(adapter, 2);
	if (cause & XGMAC_KR1)
		xgmac_intr_handler(adapter, 3);
	if (cause & PCIE)
		pcie_intr_handler(adapter);
	if (cause & MC)
		mem_intr_handler(adapter, MEM_MC);
	if (cause & EDC0)
		mem_intr_handler(adapter, MEM_EDC0);
	if (cause & EDC1)
		mem_intr_handler(adapter, MEM_EDC1);
	if (cause & LE)
		le_intr_handler(adapter);
	if (cause & TP)
		tp_intr_handler(adapter);
	if (cause & MA)
		ma_intr_handler(adapter);
	if (cause & PM_TX)
		pmtx_intr_handler(adapter);
	if (cause & PM_RX)
		pmrx_intr_handler(adapter);
	if (cause & ULP_RX)
		ulprx_intr_handler(adapter);
	if (cause & CPL_SWITCH)
		cplsw_intr_handler(adapter);
	if (cause & SGE)
		sge_intr_handler(adapter);
	if (cause & ULP_TX)
		ulptx_intr_handler(adapter);

	/* Clear the interrupts just processed for which we are the master. */
	t4_write_reg(adapter, PL_INT_CAUSE, cause & GLBL_INTR_MASK);
	(void) t4_read_reg(adapter, PL_INT_CAUSE); /* flush */
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
	u32 pf = SOURCEPF_GET(t4_read_reg(adapter, PL_WHOAMI));

	t4_write_reg(adapter, SGE_INT_ENABLE3, ERR_CPL_EXCEED_IQE_SIZE |
		     ERR_INVALID_CIDX_INC | ERR_CPL_OPCODE_0 |
		     ERR_DROPPED_DB | ERR_DATA_CPL_ON_HIGH_QID1 |
		     ERR_DATA_CPL_ON_HIGH_QID0 | ERR_BAD_DB_PIDX3 |
		     ERR_BAD_DB_PIDX2 | ERR_BAD_DB_PIDX1 |
		     ERR_BAD_DB_PIDX0 | ERR_ING_CTXT_PRIO |
		     ERR_EGR_CTXT_PRIO | INGRESS_SIZE_ERR |
		     DBFIFO_HP_INT | DBFIFO_LP_INT |
		     EGRESS_SIZE_ERR);
	t4_write_reg(adapter, MYPF_REG(PL_PF_INT_ENABLE), PF_INTR_MASK);
	t4_set_reg_field(adapter, PL_INT_MAP0, 0, 1 << pf);
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
	u32 pf = SOURCEPF_GET(t4_read_reg(adapter, PL_WHOAMI));

	t4_write_reg(adapter, MYPF_REG(PL_PF_INT_ENABLE), 0);
	t4_set_reg_field(adapter, PL_INT_MAP0, 1 << pf, 0);
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
	cmd.op_to_viid = htonl(FW_CMD_OP(FW_RSS_IND_TBL_CMD) |
			       FW_CMD_REQUEST | FW_CMD_WRITE |
			       FW_RSS_IND_TBL_CMD_VIID(viid));
	cmd.retval_len16 = htonl(FW_LEN16(cmd));

	/* each fw_rss_ind_tbl_cmd takes up to 32 entries */
	while (n > 0) {
		int nq = min(n, 32);
		__be32 *qp = &cmd.iq0_to_iq2;

		cmd.niqid = htons(nq);
		cmd.startidx = htons(start);

		start += nq;
		n -= nq;

		while (nq > 0) {
			unsigned int v;

			v = FW_RSS_IND_TBL_CMD_IQ0(*rsp);
			if (++rsp >= rsp_end)
				rsp = rspq;
			v |= FW_RSS_IND_TBL_CMD_IQ1(*rsp);
			if (++rsp >= rsp_end)
				rsp = rspq;
			v |= FW_RSS_IND_TBL_CMD_IQ2(*rsp);
			if (++rsp >= rsp_end)
				rsp = rspq;

			*qp++ = htonl(v);
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
	c.op_to_write = htonl(FW_CMD_OP(FW_RSS_GLB_CONFIG_CMD) |
			      FW_CMD_REQUEST | FW_CMD_WRITE);
	c.retval_len16 = htonl(FW_LEN16(c));
	if (mode == FW_RSS_GLB_CONFIG_CMD_MODE_MANUAL) {
		c.u.manual.mode_pkd = htonl(FW_RSS_GLB_CONFIG_CMD_MODE(mode));
	} else if (mode == FW_RSS_GLB_CONFIG_CMD_MODE_BASICVIRTUAL) {
		c.u.basicvirtual.mode_pkd =
			htonl(FW_RSS_GLB_CONFIG_CMD_MODE(mode));
		c.u.basicvirtual.synmapen_to_hashtoeplitz = htonl(flags);
	} else
		return -EINVAL;
	return t4_wr_mbox(adapter, mbox, &c, sizeof(c), NULL);
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
	u32 val[TP_MIB_TCP_RXT_SEG_LO - TP_MIB_TCP_OUT_RST + 1];

#define STAT_IDX(x) ((TP_MIB_TCP_##x) - TP_MIB_TCP_OUT_RST)
#define STAT(x)     val[STAT_IDX(x)]
#define STAT64(x)   (((u64)STAT(x##_HI) << 32) | STAT(x##_LO))

	if (v4) {
		t4_read_indirect(adap, TP_MIB_INDEX, TP_MIB_DATA, val,
				 ARRAY_SIZE(val), TP_MIB_TCP_OUT_RST);
		v4->tcpOutRsts = STAT(OUT_RST);
		v4->tcpInSegs  = STAT64(IN_SEG);
		v4->tcpOutSegs = STAT64(OUT_SEG);
		v4->tcpRetransSegs = STAT64(RXT_SEG);
	}
	if (v6) {
		t4_read_indirect(adap, TP_MIB_INDEX, TP_MIB_DATA, val,
				 ARRAY_SIZE(val), TP_MIB_TCP_V6OUT_RST);
		v6->tcpOutRsts = STAT(OUT_RST);
		v6->tcpInSegs  = STAT64(IN_SEG);
		v6->tcpOutSegs = STAT64(OUT_SEG);
		v6->tcpRetransSegs = STAT64(RXT_SEG);
	}
#undef STAT64
#undef STAT
#undef STAT_IDX
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
		t4_write_reg(adap, TP_MTU_TABLE,
			     MTUINDEX(0xff) | MTUVALUE(i));
		v = t4_read_reg(adap, TP_MTU_TABLE);
		mtus[i] = MTUVALUE_GET(v);
		if (mtu_log)
			mtu_log[i] = MTUWIDTH_GET(v);
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
	t4_write_reg(adap, TP_PIO_ADDR, addr);
	val |= t4_read_reg(adap, TP_PIO_DATA) & ~mask;
	t4_write_reg(adap, TP_PIO_DATA, val);
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
		t4_write_reg(adap, TP_MTU_TABLE, MTUINDEX(i) |
			     MTUWIDTH(log2) | MTUVALUE(mtu));

		for (w = 0; w < NCCTRL_WIN; ++w) {
			unsigned int inc;

			inc = max(((mtu - 40) * alpha[w]) / avg_pkts[w],
				  CC_MIN_INCR);

			t4_write_reg(adap, TP_CCTRL_TABLE, (i << 21) |
				     (w << 16) | (beta[w] << 13) | inc);
		}
	}
}

/**
 *	get_mps_bg_map - return the buffer groups associated with a port
 *	@adap: the adapter
 *	@idx: the port index
 *
 *	Returns a bitmap indicating which MPS buffer groups are associated
 *	with the given port.  Bit i is set if buffer group i is used by the
 *	port.
 */
static unsigned int get_mps_bg_map(struct adapter *adap, int idx)
{
	u32 n = NUMPORTS_GET(t4_read_reg(adap, MPS_CMN_CTL));

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
		"",
		"R QSFP",
		"R BP40_BA",
	};

	if (port_type < ARRAY_SIZE(port_type_description))
		return port_type_description[port_type];
	return "UNKNOWN";
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
	u32 bgmap = get_mps_bg_map(adap, idx);

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
 *	t4_wol_magic_enable - enable/disable magic packet WoL
 *	@adap: the adapter
 *	@port: the physical port index
 *	@addr: MAC address expected in magic packets, %NULL to disable
 *
 *	Enables/disables magic packet wake-on-LAN for the selected port.
 */
void t4_wol_magic_enable(struct adapter *adap, unsigned int port,
			 const u8 *addr)
{
	u32 mag_id_reg_l, mag_id_reg_h, port_cfg_reg;

	if (is_t4(adap->params.chip)) {
		mag_id_reg_l = PORT_REG(port, XGMAC_PORT_MAGIC_MACID_LO);
		mag_id_reg_h = PORT_REG(port, XGMAC_PORT_MAGIC_MACID_HI);
		port_cfg_reg = PORT_REG(port, XGMAC_PORT_CFG2);
	} else {
		mag_id_reg_l = T5_PORT_REG(port, MAC_PORT_MAGIC_MACID_LO);
		mag_id_reg_h = T5_PORT_REG(port, MAC_PORT_MAGIC_MACID_HI);
		port_cfg_reg = T5_PORT_REG(port, MAC_PORT_CFG2);
	}

	if (addr) {
		t4_write_reg(adap, mag_id_reg_l,
			     (addr[2] << 24) | (addr[3] << 16) |
			     (addr[4] << 8) | addr[5]);
		t4_write_reg(adap, mag_id_reg_h,
			     (addr[0] << 8) | addr[1]);
	}
	t4_set_reg_field(adap, port_cfg_reg, MAGICEN,
			 addr ? MAGICEN : 0);
}

/**
 *	t4_wol_pat_enable - enable/disable pattern-based WoL
 *	@adap: the adapter
 *	@port: the physical port index
 *	@map: bitmap of which HW pattern filters to set
 *	@mask0: byte mask for bytes 0-63 of a packet
 *	@mask1: byte mask for bytes 64-127 of a packet
 *	@crc: Ethernet CRC for selected bytes
 *	@enable: enable/disable switch
 *
 *	Sets the pattern filters indicated in @map to mask out the bytes
 *	specified in @mask0/@mask1 in received packets and compare the CRC of
 *	the resulting packet against @crc.  If @enable is %true pattern-based
 *	WoL is enabled, otherwise disabled.
 */
int t4_wol_pat_enable(struct adapter *adap, unsigned int port, unsigned int map,
		      u64 mask0, u64 mask1, unsigned int crc, bool enable)
{
	int i;
	u32 port_cfg_reg;

	if (is_t4(adap->params.chip))
		port_cfg_reg = PORT_REG(port, XGMAC_PORT_CFG2);
	else
		port_cfg_reg = T5_PORT_REG(port, MAC_PORT_CFG2);

	if (!enable) {
		t4_set_reg_field(adap, port_cfg_reg, PATEN, 0);
		return 0;
	}
	if (map > 0xff)
		return -EINVAL;

#define EPIO_REG(name) \
	(is_t4(adap->params.chip) ? PORT_REG(port, XGMAC_PORT_EPIO_##name) : \
	T5_PORT_REG(port, MAC_PORT_EPIO_##name))

	t4_write_reg(adap, EPIO_REG(DATA1), mask0 >> 32);
	t4_write_reg(adap, EPIO_REG(DATA2), mask1);
	t4_write_reg(adap, EPIO_REG(DATA3), mask1 >> 32);

	for (i = 0; i < NWOL_PAT; i++, map >>= 1) {
		if (!(map & 1))
			continue;

		/* write byte masks */
		t4_write_reg(adap, EPIO_REG(DATA0), mask0);
		t4_write_reg(adap, EPIO_REG(OP), ADDRESS(i) | EPIOWR);
		t4_read_reg(adap, EPIO_REG(OP));                /* flush */
		if (t4_read_reg(adap, EPIO_REG(OP)) & SF_BUSY)
			return -ETIMEDOUT;

		/* write CRC */
		t4_write_reg(adap, EPIO_REG(DATA0), crc);
		t4_write_reg(adap, EPIO_REG(OP), ADDRESS(i + 32) | EPIOWR);
		t4_read_reg(adap, EPIO_REG(OP));                /* flush */
		if (t4_read_reg(adap, EPIO_REG(OP)) & SF_BUSY)
			return -ETIMEDOUT;
	}
#undef EPIO_REG

	t4_set_reg_field(adap, PORT_REG(port, XGMAC_PORT_CFG2), 0, PATEN);
	return 0;
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
	wr->op_pkd = htonl(FW_WR_OP(FW_FILTER_WR));
	wr->len16_pkd = htonl(FW_WR_LEN16(sizeof(*wr) / 16));
	wr->tid_to_iq = htonl(V_FW_FILTER_WR_TID(ftid) |
			V_FW_FILTER_WR_NOREPLY(qid < 0));
	wr->del_filter_to_l2tix = htonl(F_FW_FILTER_WR_DEL_FILTER);
	if (qid >= 0)
		wr->rx_chan_rx_rpl_iq = htons(V_FW_FILTER_WR_RX_RPL_IQ(qid));
}

#define INIT_CMD(var, cmd, rd_wr) do { \
	(var).op_to_write = htonl(FW_CMD_OP(FW_##cmd##_CMD) | \
				  FW_CMD_REQUEST | FW_CMD_##rd_wr); \
	(var).retval_len16 = htonl(FW_LEN16(var)); \
} while (0)

int t4_fwaddrspace_write(struct adapter *adap, unsigned int mbox,
			  u32 addr, u32 val)
{
	struct fw_ldst_cmd c;

	memset(&c, 0, sizeof(c));
	c.op_to_addrspace = htonl(FW_CMD_OP(FW_LDST_CMD) | FW_CMD_REQUEST |
			    FW_CMD_WRITE |
			    FW_LDST_CMD_ADDRSPACE(FW_LDST_ADDRSPC_FIRMWARE));
	c.cycles_to_len16 = htonl(FW_LEN16(c));
	c.u.addrval.addr = htonl(addr);
	c.u.addrval.val = htonl(val);

	return t4_wr_mbox(adap, mbox, &c, sizeof(c), NULL);
}

/**
 *     t4_mem_win_read_len - read memory through PCIE memory window
 *     @adap: the adapter
 *     @addr: address of first byte requested aligned on 32b.
 *     @data: len bytes to hold the data read
 *     @len: amount of data to read from window.  Must be <=
 *            MEMWIN0_APERATURE after adjusting for 16B for T4 and
 *            128B for T5 alignment requirements of the the memory window.
 *
 *     Read len bytes of data from MC starting at @addr.
 */
int t4_mem_win_read_len(struct adapter *adap, u32 addr, __be32 *data, int len)
{
	int i, off;
	u32 win_pf = is_t4(adap->params.chip) ? 0 : V_PFNUM(adap->fn);

	/* Align on a 2KB boundary.
	 */
	off = addr & MEMWIN0_APERTURE;
	if ((addr & 3) || (len + off) > MEMWIN0_APERTURE)
		return -EINVAL;

	t4_write_reg(adap, PCIE_MEM_ACCESS_OFFSET,
		     (addr & ~MEMWIN0_APERTURE) | win_pf);
	t4_read_reg(adap, PCIE_MEM_ACCESS_OFFSET);

	for (i = 0; i < len; i += 4)
		*data++ = (__force __be32) t4_read_reg(adap,
						(MEMWIN0_BASE + off + i));

	return 0;
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
	struct fw_ldst_cmd c;

	memset(&c, 0, sizeof(c));
	c.op_to_addrspace = htonl(FW_CMD_OP(FW_LDST_CMD) | FW_CMD_REQUEST |
		FW_CMD_READ | FW_LDST_CMD_ADDRSPACE(FW_LDST_ADDRSPC_MDIO));
	c.cycles_to_len16 = htonl(FW_LEN16(c));
	c.u.mdio.paddr_mmd = htons(FW_LDST_CMD_PADDR(phy_addr) |
				   FW_LDST_CMD_MMD(mmd));
	c.u.mdio.raddr = htons(reg);

	ret = t4_wr_mbox(adap, mbox, &c, sizeof(c), &c);
	if (ret == 0)
		*valp = ntohs(c.u.mdio.rval);
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
	struct fw_ldst_cmd c;

	memset(&c, 0, sizeof(c));
	c.op_to_addrspace = htonl(FW_CMD_OP(FW_LDST_CMD) | FW_CMD_REQUEST |
		FW_CMD_WRITE | FW_LDST_CMD_ADDRSPACE(FW_LDST_ADDRSPC_MDIO));
	c.cycles_to_len16 = htonl(FW_LEN16(c));
	c.u.mdio.paddr_mmd = htons(FW_LDST_CMD_PADDR(phy_addr) |
				   FW_LDST_CMD_MMD(mmd));
	c.u.mdio.raddr = htons(reg);
	c.u.mdio.rval = htons(val);

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
		SGE_DEBUG_DATA_LOW_INDEX_2,
		SGE_DEBUG_DATA_LOW_INDEX_3,
		SGE_DEBUG_DATA_HIGH_INDEX_10,
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
	c.err_to_clearinit = htonl(
		FW_HELLO_CMD_MASTERDIS(master == MASTER_CANT) |
		FW_HELLO_CMD_MASTERFORCE(master == MASTER_MUST) |
		FW_HELLO_CMD_MBMASTER(master == MASTER_MUST ? mbox :
				      FW_HELLO_CMD_MBMASTER_MASK) |
		FW_HELLO_CMD_MBASYNCNOT(evt_mbox) |
		FW_HELLO_CMD_STAGE(fw_hello_cmd_stage_os) |
		FW_HELLO_CMD_CLEARINIT);

	/*
	 * Issue the HELLO command to the firmware.  If it's not successful
	 * but indicates that we got a "busy" or "timeout" condition, retry
	 * the HELLO until we exhaust our retry limit.
	 */
	ret = t4_wr_mbox(adap, mbox, &c, sizeof(c), &c);
	if (ret < 0) {
		if ((ret == -EBUSY || ret == -ETIMEDOUT) && retries-- > 0)
			goto retry;
		return ret;
	}

	v = ntohl(c.err_to_clearinit);
	master_mbox = FW_HELLO_CMD_MBMASTER_GET(v);
	if (state) {
		if (v & FW_HELLO_CMD_ERR)
			*state = DEV_STATE_ERR;
		else if (v & FW_HELLO_CMD_INIT)
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
	 * FW_PCIE_FW_MASTER_MASK so the test below will work ...
	 */
	if ((v & (FW_HELLO_CMD_ERR|FW_HELLO_CMD_INIT)) == 0 &&
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
			pcie_fw = t4_read_reg(adap, MA_PCIE_FW);
			if (!(pcie_fw & (FW_PCIE_FW_ERR|FW_PCIE_FW_INIT))) {
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
				if (pcie_fw & FW_PCIE_FW_ERR)
					*state = DEV_STATE_ERR;
				else if (pcie_fw & FW_PCIE_FW_INIT)
					*state = DEV_STATE_INIT;
			}

			/*
			 * If we arrived before a Master PF was selected and
			 * there's not a valid Master PF, grab its identity
			 * for our caller.
			 */
			if (master_mbox == FW_PCIE_FW_MASTER_MASK &&
			    (pcie_fw & FW_PCIE_FW_MASTER_VLD))
				master_mbox = FW_PCIE_FW_MASTER_GET(pcie_fw);
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
	c.val = htonl(reset);
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
 *	FW_PCIE_FW_MASTER_MASK).
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
	if (mbox <= FW_PCIE_FW_MASTER_MASK) {
		struct fw_reset_cmd c;

		memset(&c, 0, sizeof(c));
		INIT_CMD(c, RESET, WRITE);
		c.val = htonl(PIORST | PIORSTMODE);
		c.halt_pkd = htonl(FW_RESET_CMD_HALT(1U));
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
		t4_set_reg_field(adap, CIM_BOOT_CFG, UPCRST, UPCRST);
		t4_set_reg_field(adap, PCIE_FW, FW_PCIE_FW_HALT,
				 FW_PCIE_FW_HALT);
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
		t4_set_reg_field(adap, PCIE_FW, FW_PCIE_FW_HALT, 0);

		/*
		 * If we've been given a valid mailbox, first try to get the
		 * firmware to do the RESET.  If that works, great and we can
		 * return success.  Otherwise, if we haven't been given a
		 * valid mailbox or the RESET command failed, fall back to
		 * hitting the chip with a hammer.
		 */
		if (mbox <= FW_PCIE_FW_MASTER_MASK) {
			t4_set_reg_field(adap, CIM_BOOT_CFG, UPCRST, 0);
			msleep(100);
			if (t4_fw_reset(adap, mbox,
					PIORST | PIORSTMODE) == 0)
				return 0;
		}

		t4_write_reg(adap, PL_RST, PIORST | PIORSTMODE);
		msleep(2000);
	} else {
		int ms;

		t4_set_reg_field(adap, CIM_BOOT_CFG, UPCRST, 0);
		for (ms = 0; ms < FW_CMD_MAX_TIMEOUT; ) {
			if (!(t4_read_reg(adap, PCIE_FW) & FW_PCIE_FW_HALT))
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
static int t4_fw_upgrade(struct adapter *adap, unsigned int mbox,
			 const u8 *fw_data, unsigned int size, int force)
{
	const struct fw_hdr *fw_hdr = (const struct fw_hdr *)fw_data;
	int reset, ret;

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
	reset = ((ntohl(fw_hdr->flags) & FW_HDR_FLAGS_RESET_HALT) == 0);
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

	t4_write_reg(adap, SGE_HOST_PAGE_SIZE,
		     HOSTPAGESIZEPF0(sge_hps) |
		     HOSTPAGESIZEPF1(sge_hps) |
		     HOSTPAGESIZEPF2(sge_hps) |
		     HOSTPAGESIZEPF3(sge_hps) |
		     HOSTPAGESIZEPF4(sge_hps) |
		     HOSTPAGESIZEPF5(sge_hps) |
		     HOSTPAGESIZEPF6(sge_hps) |
		     HOSTPAGESIZEPF7(sge_hps));

	t4_set_reg_field(adap, SGE_CONTROL,
			 INGPADBOUNDARY_MASK |
			 EGRSTATUSPAGESIZE_MASK,
			 INGPADBOUNDARY(fl_align_log - 5) |
			 EGRSTATUSPAGESIZE(stat_len != 64));

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
	 * Padding boundry.  All of these are accommodated in the Factory
	 * Default Firmware Configuration File but we need to adjust it for
	 * this host's cache line size.
	 */
	t4_write_reg(adap, SGE_FL_BUFFER_SIZE0, page_size);
	t4_write_reg(adap, SGE_FL_BUFFER_SIZE2,
		     (t4_read_reg(adap, SGE_FL_BUFFER_SIZE2) + fl_align-1)
		     & ~(fl_align-1));
	t4_write_reg(adap, SGE_FL_BUFFER_SIZE3,
		     (t4_read_reg(adap, SGE_FL_BUFFER_SIZE3) + fl_align-1)
		     & ~(fl_align-1));

	t4_write_reg(adap, ULP_RX_TDDP_PSZ, HPZ0(page_shift - 12));

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
 *	t4_query_params - query FW or device parameters
 *	@adap: the adapter
 *	@mbox: mailbox to use for the FW command
 *	@pf: the PF
 *	@vf: the VF
 *	@nparams: the number of parameters
 *	@params: the parameter names
 *	@val: the parameter values
 *
 *	Reads the value of FW or device parameters.  Up to 7 parameters can be
 *	queried at once.
 */
int t4_query_params(struct adapter *adap, unsigned int mbox, unsigned int pf,
		    unsigned int vf, unsigned int nparams, const u32 *params,
		    u32 *val)
{
	int i, ret;
	struct fw_params_cmd c;
	__be32 *p = &c.param[0].mnem;

	if (nparams > 7)
		return -EINVAL;

	memset(&c, 0, sizeof(c));
	c.op_to_vfn = htonl(FW_CMD_OP(FW_PARAMS_CMD) | FW_CMD_REQUEST |
			    FW_CMD_READ | FW_PARAMS_CMD_PFN(pf) |
			    FW_PARAMS_CMD_VFN(vf));
	c.retval_len16 = htonl(FW_LEN16(c));
	for (i = 0; i < nparams; i++, p += 2)
		*p = htonl(*params++);

	ret = t4_wr_mbox(adap, mbox, &c, sizeof(c), &c);
	if (ret == 0)
		for (i = 0, p = &c.param[0].val; i < nparams; i++, p += 2)
			*val++ = ntohl(*p);
	return ret;
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
	struct fw_params_cmd c;
	__be32 *p = &c.param[0].mnem;

	if (nparams > 7)
		return -EINVAL;

	memset(&c, 0, sizeof(c));
	c.op_to_vfn = htonl(FW_CMD_OP(FW_PARAMS_CMD) | FW_CMD_REQUEST |
			    FW_CMD_WRITE | FW_PARAMS_CMD_PFN(pf) |
			    FW_PARAMS_CMD_VFN(vf));
	c.retval_len16 = htonl(FW_LEN16(c));
	while (nparams--) {
		*p++ = htonl(*params++);
		*p++ = htonl(*val++);
	}

	return t4_wr_mbox(adap, mbox, &c, sizeof(c), NULL);
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
	c.op_to_vfn = htonl(FW_CMD_OP(FW_PFVF_CMD) | FW_CMD_REQUEST |
			    FW_CMD_WRITE | FW_PFVF_CMD_PFN(pf) |
			    FW_PFVF_CMD_VFN(vf));
	c.retval_len16 = htonl(FW_LEN16(c));
	c.niqflint_niq = htonl(FW_PFVF_CMD_NIQFLINT(rxqi) |
			       FW_PFVF_CMD_NIQ(rxq));
	c.type_to_neq = htonl(FW_PFVF_CMD_CMASK(cmask) |
			       FW_PFVF_CMD_PMASK(pmask) |
			       FW_PFVF_CMD_NEQ(txq));
	c.tc_to_nexactf = htonl(FW_PFVF_CMD_TC(tc) | FW_PFVF_CMD_NVI(vi) |
				FW_PFVF_CMD_NEXACTF(nexact));
	c.r_caps_to_nethctrl = htonl(FW_PFVF_CMD_R_CAPS(rcaps) |
				     FW_PFVF_CMD_WX_CAPS(wxcaps) |
				     FW_PFVF_CMD_NETHCTRL(txq_eth_ctrl));
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
	c.op_to_vfn = htonl(FW_CMD_OP(FW_VI_CMD) | FW_CMD_REQUEST |
			    FW_CMD_WRITE | FW_CMD_EXEC |
			    FW_VI_CMD_PFN(pf) | FW_VI_CMD_VFN(vf));
	c.alloc_to_len16 = htonl(FW_VI_CMD_ALLOC | FW_LEN16(c));
	c.portid_pkd = FW_VI_CMD_PORTID(port);
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
		*rss_size = FW_VI_CMD_RSSSIZE_GET(ntohs(c.rsssize_pkd));
	return FW_VI_CMD_VIID_GET(ntohs(c.type_viid));
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
		promisc = FW_VI_RXMODE_CMD_PROMISCEN_MASK;
	if (all_multi < 0)
		all_multi = FW_VI_RXMODE_CMD_ALLMULTIEN_MASK;
	if (bcast < 0)
		bcast = FW_VI_RXMODE_CMD_BROADCASTEN_MASK;
	if (vlanex < 0)
		vlanex = FW_VI_RXMODE_CMD_VLANEXEN_MASK;

	memset(&c, 0, sizeof(c));
	c.op_to_viid = htonl(FW_CMD_OP(FW_VI_RXMODE_CMD) | FW_CMD_REQUEST |
			     FW_CMD_WRITE | FW_VI_RXMODE_CMD_VIID(viid));
	c.retval_len16 = htonl(FW_LEN16(c));
	c.mtu_to_vlanexen = htonl(FW_VI_RXMODE_CMD_MTU(mtu) |
				  FW_VI_RXMODE_CMD_PROMISCEN(promisc) |
				  FW_VI_RXMODE_CMD_ALLMULTIEN(all_multi) |
				  FW_VI_RXMODE_CMD_BROADCASTEN(bcast) |
				  FW_VI_RXMODE_CMD_VLANEXEN(vlanex));
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
	int i, ret;
	struct fw_vi_mac_cmd c;
	struct fw_vi_mac_exact *p;
	unsigned int max_naddr = is_t4(adap->params.chip) ?
				       NUM_MPS_CLS_SRAM_L_INSTANCES :
				       NUM_MPS_T5_CLS_SRAM_L_INSTANCES;

	if (naddr > 7)
		return -EINVAL;

	memset(&c, 0, sizeof(c));
	c.op_to_viid = htonl(FW_CMD_OP(FW_VI_MAC_CMD) | FW_CMD_REQUEST |
			     FW_CMD_WRITE | (free ? FW_CMD_EXEC : 0) |
			     FW_VI_MAC_CMD_VIID(viid));
	c.freemacs_to_len16 = htonl(FW_VI_MAC_CMD_FREEMACS(free) |
				    FW_CMD_LEN16((naddr + 2) / 2));

	for (i = 0, p = c.u.exact; i < naddr; i++, p++) {
		p->valid_to_idx = htons(FW_VI_MAC_CMD_VALID |
				      FW_VI_MAC_CMD_IDX(FW_VI_MAC_ADD_MAC));
		memcpy(p->macaddr, addr[i], sizeof(p->macaddr));
	}

	ret = t4_wr_mbox_meat(adap, mbox, &c, sizeof(c), &c, sleep_ok);
	if (ret)
		return ret;

	for (i = 0, p = c.u.exact; i < naddr; i++, p++) {
		u16 index = FW_VI_MAC_CMD_IDX_GET(ntohs(p->valid_to_idx));

		if (idx)
			idx[i] = index >= max_naddr ? 0xffff : index;
		if (index < max_naddr)
			ret++;
		else if (hash)
			*hash |= (1ULL << hash_mac_addr(addr[i]));
	}
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
	unsigned int max_mac_addr = is_t4(adap->params.chip) ?
				    NUM_MPS_CLS_SRAM_L_INSTANCES :
				    NUM_MPS_T5_CLS_SRAM_L_INSTANCES;

	if (idx < 0)                             /* new allocation */
		idx = persist ? FW_VI_MAC_ADD_PERSIST_MAC : FW_VI_MAC_ADD_MAC;
	mode = add_smt ? FW_VI_MAC_SMT_AND_MPSTCAM : FW_VI_MAC_MPS_TCAM_ENTRY;

	memset(&c, 0, sizeof(c));
	c.op_to_viid = htonl(FW_CMD_OP(FW_VI_MAC_CMD) | FW_CMD_REQUEST |
			     FW_CMD_WRITE | FW_VI_MAC_CMD_VIID(viid));
	c.freemacs_to_len16 = htonl(FW_CMD_LEN16(1));
	p->valid_to_idx = htons(FW_VI_MAC_CMD_VALID |
				FW_VI_MAC_CMD_SMAC_RESULT(mode) |
				FW_VI_MAC_CMD_IDX(idx));
	memcpy(p->macaddr, addr, sizeof(p->macaddr));

	ret = t4_wr_mbox(adap, mbox, &c, sizeof(c), &c);
	if (ret == 0) {
		ret = FW_VI_MAC_CMD_IDX_GET(ntohs(p->valid_to_idx));
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
	c.op_to_viid = htonl(FW_CMD_OP(FW_VI_MAC_CMD) | FW_CMD_REQUEST |
			     FW_CMD_WRITE | FW_VI_ENABLE_CMD_VIID(viid));
	c.freemacs_to_len16 = htonl(FW_VI_MAC_CMD_HASHVECEN |
				    FW_VI_MAC_CMD_HASHUNIEN(ucast) |
				    FW_CMD_LEN16(1));
	c.u.hash.hashvec = cpu_to_be64(vec);
	return t4_wr_mbox_meat(adap, mbox, &c, sizeof(c), NULL, sleep_ok);
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
	struct fw_vi_enable_cmd c;

	memset(&c, 0, sizeof(c));
	c.op_to_viid = htonl(FW_CMD_OP(FW_VI_ENABLE_CMD) | FW_CMD_REQUEST |
			     FW_CMD_EXEC | FW_VI_ENABLE_CMD_VIID(viid));
	c.ien_to_len16 = htonl(FW_VI_ENABLE_CMD_IEN(rx_en) |
			       FW_VI_ENABLE_CMD_EEN(tx_en) | FW_LEN16(c));
	return t4_wr_mbox(adap, mbox, &c, sizeof(c), NULL);
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
	c.op_to_viid = htonl(FW_CMD_OP(FW_VI_ENABLE_CMD) | FW_CMD_REQUEST |
			     FW_CMD_EXEC | FW_VI_ENABLE_CMD_VIID(viid));
	c.ien_to_len16 = htonl(FW_VI_ENABLE_CMD_LED | FW_LEN16(c));
	c.blinkdur = htons(nblinks);
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
	c.op_to_vfn = htonl(FW_CMD_OP(FW_IQ_CMD) | FW_CMD_REQUEST |
			    FW_CMD_EXEC | FW_IQ_CMD_PFN(pf) |
			    FW_IQ_CMD_VFN(vf));
	c.alloc_to_len16 = htonl(FW_IQ_CMD_FREE | FW_LEN16(c));
	c.type_to_iqandstindex = htonl(FW_IQ_CMD_TYPE(iqtype));
	c.iqid = htons(iqid);
	c.fl0id = htons(fl0id);
	c.fl1id = htons(fl1id);
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
	c.op_to_vfn = htonl(FW_CMD_OP(FW_EQ_ETH_CMD) | FW_CMD_REQUEST |
			    FW_CMD_EXEC | FW_EQ_ETH_CMD_PFN(pf) |
			    FW_EQ_ETH_CMD_VFN(vf));
	c.alloc_to_len16 = htonl(FW_EQ_ETH_CMD_FREE | FW_LEN16(c));
	c.eqid_pkd = htonl(FW_EQ_ETH_CMD_EQID(eqid));
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
	c.op_to_vfn = htonl(FW_CMD_OP(FW_EQ_CTRL_CMD) | FW_CMD_REQUEST |
			    FW_CMD_EXEC | FW_EQ_CTRL_CMD_PFN(pf) |
			    FW_EQ_CTRL_CMD_VFN(vf));
	c.alloc_to_len16 = htonl(FW_EQ_CTRL_CMD_FREE | FW_LEN16(c));
	c.cmpliqid_eqid = htonl(FW_EQ_CTRL_CMD_EQID(eqid));
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
	c.op_to_vfn = htonl(FW_CMD_OP(FW_EQ_OFLD_CMD) | FW_CMD_REQUEST |
			    FW_CMD_EXEC | FW_EQ_OFLD_CMD_PFN(pf) |
			    FW_EQ_OFLD_CMD_VFN(vf));
	c.alloc_to_len16 = htonl(FW_EQ_OFLD_CMD_FREE | FW_LEN16(c));
	c.eqid_pkd = htonl(FW_EQ_OFLD_CMD_EQID(eqid));
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
		int chan = FW_PORT_CMD_PORTID_GET(ntohl(p->op_to_portid));
		int port = adap->chan_map[chan];
		struct port_info *pi = adap2pinfo(adap, port);
		struct link_config *lc = &pi->link_cfg;
		u32 stat = ntohl(p->u.info.lstatus_to_modtype);
		int link_ok = (stat & FW_PORT_CMD_LSTATUS) != 0;
		u32 mod = FW_PORT_CMD_MODTYPE_GET(stat);

		if (stat & FW_PORT_CMD_RXPAUSE)
			fc |= PAUSE_RX;
		if (stat & FW_PORT_CMD_TXPAUSE)
			fc |= PAUSE_TX;
		if (stat & FW_PORT_CMD_LSPEED(FW_PORT_CAP_SPEED_100M))
			speed = 100;
		else if (stat & FW_PORT_CMD_LSPEED(FW_PORT_CAP_SPEED_1G))
			speed = 1000;
		else if (stat & FW_PORT_CMD_LSPEED(FW_PORT_CAP_SPEED_10G))
			speed = 10000;
		else if (stat & FW_PORT_CMD_LSPEED(FW_PORT_CAP_SPEED_40G))
			speed = 40000;

		if (link_ok != lc->link_ok || speed != lc->speed ||
		    fc != lc->fc) {                    /* something changed */
			lc->link_ok = link_ok;
			lc->speed = speed;
			lc->fc = fc;
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

int t4_wait_dev_ready(struct adapter *adap)
{
	if (t4_read_reg(adap, PL_WHOAMI) != 0xffffffff)
		return 0;
	msleep(500);
	return t4_read_reg(adap, PL_WHOAMI) != 0xffffffff ? 0 : -EIO;
}

static int get_flash_params(struct adapter *adap)
{
	int ret;
	u32 info;

	ret = sf1_write(adap, 1, 1, 0, SF_RD_ID);
	if (!ret)
		ret = sf1_read(adap, 3, 0, 1, &info);
	t4_write_reg(adap, SF_OP, 0);                    /* unlock SF */
	if (ret)
		return ret;

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
		t4_read_reg(adap, CIM_BOOT_CFG) & BOOTADDR_MASK;
	return 0;
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

	ret = t4_wait_dev_ready(adapter);
	if (ret < 0)
		return ret;

	get_pci_mode(adapter, &adapter->params.pci);
	pl_rev = G_REV(t4_read_reg(adapter, PL_REV));

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
		break;
	case CHELSIO_T5:
		adapter->params.chip |= CHELSIO_CHIP_CODE(CHELSIO_T5, pl_rev);
		break;
	default:
		dev_err(adapter->pdev_dev, "Device %d is not supported\n",
			device_id);
		return -EINVAL;
	}

	init_cong_ctrl(adapter->params.a_wnd, adapter->params.b_wnd);

	/*
	 * Default port for debugging in case we can't reach FW.
	 */
	adapter->params.nports = 1;
	adapter->params.portvec = 1;
	adapter->params.vpd.cclk = 50000;
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

	v = t4_read_reg(adap, TP_TIMER_RESOLUTION);
	adap->params.tp.tre = TIMERRESOLUTION_GET(v);
	adap->params.tp.dack_re = DELAYEDACKRESOLUTION_GET(v);

	/* MODQ_REQ_MAP defaults to setting queues 0-3 to chan 0-3 */
	for (chan = 0; chan < NCHAN; chan++)
		adap->params.tp.tx_modq[chan] = chan;

	/* Cache the adapter's Compressed Filter Mode and global Incress
	 * Configuration.
	 */
	t4_read_indirect(adap, TP_PIO_ADDR, TP_PIO_DATA,
			 &adap->params.tp.vlan_pri_map, 1,
			 TP_VLAN_PRI_MAP);
	t4_read_indirect(adap, TP_PIO_ADDR, TP_PIO_DATA,
			 &adap->params.tp.ingress_config, 1,
			 TP_INGRESS_CONFIG);

	/* Now that we have TP_VLAN_PRI_MAP cached, we can calculate the field
	 * shift positions of several elements of the Compressed Filter Tuple
	 * for this adapter which we need frequently ...
	 */
	adap->params.tp.vlan_shift = t4_filter_field_shift(adap, F_VLAN);
	adap->params.tp.vnic_shift = t4_filter_field_shift(adap, F_VNIC_ID);
	adap->params.tp.port_shift = t4_filter_field_shift(adap, F_PORT);
	adap->params.tp.protocol_shift = t4_filter_field_shift(adap,
							       F_PROTOCOL);

	/* If TP_INGRESS_CONFIG.VNID == 0, then TP_VLAN_PRI_MAP.VNIC_ID
	 * represents the presense of an Outer VLAN instead of a VNIC ID.
	 */
	if ((adap->params.tp.ingress_config & F_VNIC) == 0)
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
		case F_FCOE:
			field_shift += W_FT_FCOE;
			break;
		case F_PORT:
			field_shift += W_FT_PORT;
			break;
		case F_VNIC_ID:
			field_shift += W_FT_VNIC_ID;
			break;
		case F_VLAN:
			field_shift += W_FT_VLAN;
			break;
		case F_TOS:
			field_shift += W_FT_TOS;
			break;
		case F_PROTOCOL:
			field_shift += W_FT_PROTOCOL;
			break;
		case F_ETHERTYPE:
			field_shift += W_FT_ETHERTYPE;
			break;
		case F_MACMATCH:
			field_shift += W_FT_MACMATCH;
			break;
		case F_MPSHITTYPE:
			field_shift += W_FT_MPSHITTYPE;
			break;
		case F_FRAGMENTATION:
			field_shift += W_FT_FRAGMENTATION;
			break;
		}
	}
	return field_shift;
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

		c.op_to_portid = htonl(FW_CMD_OP(FW_PORT_CMD) |
				       FW_CMD_REQUEST | FW_CMD_READ |
				       FW_PORT_CMD_PORTID(j));
		c.action_to_len16 = htonl(
			FW_PORT_CMD_ACTION(FW_PORT_ACTION_GET_PORT_INFO) |
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

		ret = ntohl(c.u.info.lstatus_to_modtype);
		p->mdio_addr = (ret & FW_PORT_CMD_MDIOCAP) ?
			FW_PORT_CMD_MDIOADDR_GET(ret) : -1;
		p->port_type = FW_PORT_CMD_PTYPE_GET(ret);
		p->mod_type = FW_PORT_MOD_TYPE_NA;

		rvc.op_to_viid = htonl(FW_CMD_OP(FW_RSS_VI_CONFIG_CMD) |
				       FW_CMD_REQUEST | FW_CMD_READ |
				       FW_RSS_VI_CONFIG_CMD_VIID(p->viid));
		rvc.retval_len16 = htonl(FW_LEN16(rvc));
		ret = t4_wr_mbox(adap, mbox, &rvc, sizeof(rvc), &rvc);
		if (ret)
			return ret;
		p->rss_mode = ntohl(rvc.u.basicvirtual.defaultq_to_udpen);

		init_link_config(&p->link_cfg, ntohs(c.u.info.pcap));
		j++;
	}
	return 0;
}
