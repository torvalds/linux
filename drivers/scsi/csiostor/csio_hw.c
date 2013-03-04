/*
 * This file is part of the Chelsio FCoE driver for Linux.
 *
 * Copyright (c) 2008-2012 Chelsio Communications, Inc. All rights reserved.
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

#include <linux/pci.h>
#include <linux/pci_regs.h>
#include <linux/firmware.h>
#include <linux/stddef.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/compiler.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/log2.h>

#include "csio_hw.h"
#include "csio_lnode.h"
#include "csio_rnode.h"

int csio_force_master;
int csio_dbg_level = 0xFEFF;
unsigned int csio_port_mask = 0xf;

/* Default FW event queue entries. */
static uint32_t csio_evtq_sz = CSIO_EVTQ_SIZE;

/* Default MSI param level */
int csio_msi = 2;

/* FCoE function instances */
static int dev_num;

/* FCoE Adapter types & its description */
static const struct csio_adap_desc csio_fcoe_adapters[] = {
	{"T440-Dbg 10G", "Chelsio T440-Dbg 10G [FCoE]"},
	{"T420-CR 10G", "Chelsio T420-CR 10G [FCoE]"},
	{"T422-CR 10G/1G", "Chelsio T422-CR 10G/1G [FCoE]"},
	{"T440-CR 10G", "Chelsio T440-CR 10G [FCoE]"},
	{"T420-BCH 10G", "Chelsio T420-BCH 10G [FCoE]"},
	{"T440-BCH 10G", "Chelsio T440-BCH 10G [FCoE]"},
	{"T440-CH 10G", "Chelsio T440-CH 10G [FCoE]"},
	{"T420-SO 10G", "Chelsio T420-SO 10G [FCoE]"},
	{"T420-CX4 10G", "Chelsio T420-CX4 10G [FCoE]"},
	{"T420-BT 10G", "Chelsio T420-BT 10G [FCoE]"},
	{"T404-BT 1G", "Chelsio T404-BT 1G [FCoE]"},
	{"B420-SR 10G", "Chelsio B420-SR 10G [FCoE]"},
	{"B404-BT 1G", "Chelsio B404-BT 1G [FCoE]"},
	{"T480-CR 10G", "Chelsio T480-CR 10G [FCoE]"},
	{"T440-LP-CR 10G", "Chelsio T440-LP-CR 10G [FCoE]"},
	{"T4 FPGA", "Chelsio T4 FPGA [FCoE]"}
};

static void csio_mgmtm_cleanup(struct csio_mgmtm *);
static void csio_hw_mbm_cleanup(struct csio_hw *);

/* State machine forward declarations */
static void csio_hws_uninit(struct csio_hw *, enum csio_hw_ev);
static void csio_hws_configuring(struct csio_hw *, enum csio_hw_ev);
static void csio_hws_initializing(struct csio_hw *, enum csio_hw_ev);
static void csio_hws_ready(struct csio_hw *, enum csio_hw_ev);
static void csio_hws_quiescing(struct csio_hw *, enum csio_hw_ev);
static void csio_hws_quiesced(struct csio_hw *, enum csio_hw_ev);
static void csio_hws_resetting(struct csio_hw *, enum csio_hw_ev);
static void csio_hws_removing(struct csio_hw *, enum csio_hw_ev);
static void csio_hws_pcierr(struct csio_hw *, enum csio_hw_ev);

static void csio_hw_initialize(struct csio_hw *hw);
static void csio_evtq_stop(struct csio_hw *hw);
static void csio_evtq_start(struct csio_hw *hw);

int csio_is_hw_ready(struct csio_hw *hw)
{
	return csio_match_state(hw, csio_hws_ready);
}

int csio_is_hw_removing(struct csio_hw *hw)
{
	return csio_match_state(hw, csio_hws_removing);
}


/*
 *	csio_hw_wait_op_done_val - wait until an operation is completed
 *	@hw: the HW module
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
static int
csio_hw_wait_op_done_val(struct csio_hw *hw, int reg, uint32_t mask,
			 int polarity, int attempts, int delay, uint32_t *valp)
{
	uint32_t val;
	while (1) {
		val = csio_rd_reg32(hw, reg);

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

void
csio_set_reg_field(struct csio_hw *hw, uint32_t reg, uint32_t mask,
		   uint32_t value)
{
	uint32_t val = csio_rd_reg32(hw, reg) & ~mask;

	csio_wr_reg32(hw, val | value, reg);
	/* Flush */
	csio_rd_reg32(hw, reg);

}

/*
 *	csio_hw_mc_read - read from MC through backdoor accesses
 *	@hw: the hw module
 *	@addr: address of first byte requested
 *	@data: 64 bytes of data containing the requested address
 *	@ecc: where to store the corresponding 64-bit ECC word
 *
 *	Read 64 bytes of data from MC starting at a 64-byte-aligned address
 *	that covers the requested address @addr.  If @parity is not %NULL it
 *	is assigned the 64-bit ECC word for the read data.
 */
int
csio_hw_mc_read(struct csio_hw *hw, uint32_t addr, __be32 *data,
		uint64_t *ecc)
{
	int i;

	if (csio_rd_reg32(hw, MC_BIST_CMD) & START_BIST)
		return -EBUSY;
	csio_wr_reg32(hw, addr & ~0x3fU, MC_BIST_CMD_ADDR);
	csio_wr_reg32(hw, 64, MC_BIST_CMD_LEN);
	csio_wr_reg32(hw, 0xc, MC_BIST_DATA_PATTERN);
	csio_wr_reg32(hw, BIST_OPCODE(1) | START_BIST |  BIST_CMD_GAP(1),
		      MC_BIST_CMD);
	i = csio_hw_wait_op_done_val(hw, MC_BIST_CMD, START_BIST,
		 0, 10, 1, NULL);
	if (i)
		return i;

#define MC_DATA(i) MC_BIST_STATUS_REG(MC_BIST_STATUS_RDATA, i)

	for (i = 15; i >= 0; i--)
		*data++ = htonl(csio_rd_reg32(hw, MC_DATA(i)));
	if (ecc)
		*ecc = csio_rd_reg64(hw, MC_DATA(16));
#undef MC_DATA
	return 0;
}

/*
 *	csio_hw_edc_read - read from EDC through backdoor accesses
 *	@hw: the hw module
 *	@idx: which EDC to access
 *	@addr: address of first byte requested
 *	@data: 64 bytes of data containing the requested address
 *	@ecc: where to store the corresponding 64-bit ECC word
 *
 *	Read 64 bytes of data from EDC starting at a 64-byte-aligned address
 *	that covers the requested address @addr.  If @parity is not %NULL it
 *	is assigned the 64-bit ECC word for the read data.
 */
int
csio_hw_edc_read(struct csio_hw *hw, int idx, uint32_t addr, __be32 *data,
		uint64_t *ecc)
{
	int i;

	idx *= EDC_STRIDE;
	if (csio_rd_reg32(hw, EDC_BIST_CMD + idx) & START_BIST)
		return -EBUSY;
	csio_wr_reg32(hw, addr & ~0x3fU, EDC_BIST_CMD_ADDR + idx);
	csio_wr_reg32(hw, 64, EDC_BIST_CMD_LEN + idx);
	csio_wr_reg32(hw, 0xc, EDC_BIST_DATA_PATTERN + idx);
	csio_wr_reg32(hw, BIST_OPCODE(1) | BIST_CMD_GAP(1) | START_BIST,
		     EDC_BIST_CMD + idx);
	i = csio_hw_wait_op_done_val(hw, EDC_BIST_CMD + idx, START_BIST,
		 0, 10, 1, NULL);
	if (i)
		return i;

#define EDC_DATA(i) (EDC_BIST_STATUS_REG(EDC_BIST_STATUS_RDATA, i) + idx)

	for (i = 15; i >= 0; i--)
		*data++ = htonl(csio_rd_reg32(hw, EDC_DATA(i)));
	if (ecc)
		*ecc = csio_rd_reg64(hw, EDC_DATA(16));
#undef EDC_DATA
	return 0;
}

/*
 *      csio_mem_win_rw - read/write memory through PCIE memory window
 *      @hw: the adapter
 *      @addr: address of first byte requested
 *      @data: MEMWIN0_APERTURE bytes of data containing the requested address
 *      @dir: direction of transfer 1 => read, 0 => write
 *
 *      Read/write MEMWIN0_APERTURE bytes of data from MC starting at a
 *      MEMWIN0_APERTURE-byte-aligned address that covers the requested
 *      address @addr.
 */
static int
csio_mem_win_rw(struct csio_hw *hw, u32 addr, u32 *data, int dir)
{
	int i;

	/*
	 * Setup offset into PCIE memory window.  Address must be a
	 * MEMWIN0_APERTURE-byte-aligned address.  (Read back MA register to
	 * ensure that changes propagate before we attempt to use the new
	 * values.)
	 */
	csio_wr_reg32(hw, addr & ~(MEMWIN0_APERTURE - 1),
			PCIE_MEM_ACCESS_OFFSET);
	csio_rd_reg32(hw, PCIE_MEM_ACCESS_OFFSET);

	/* Collecting data 4 bytes at a time upto MEMWIN0_APERTURE */
	for (i = 0; i < MEMWIN0_APERTURE; i = i + sizeof(__be32)) {
		if (dir)
			*data++ = csio_rd_reg32(hw, (MEMWIN0_BASE + i));
		else
			csio_wr_reg32(hw, *data++, (MEMWIN0_BASE + i));
	}

	return 0;
}

/*
 *      csio_memory_rw - read/write EDC 0, EDC 1 or MC via PCIE memory window
 *      @hw: the csio_hw
 *      @mtype: memory type: MEM_EDC0, MEM_EDC1 or MEM_MC
 *      @addr: address within indicated memory type
 *      @len: amount of memory to transfer
 *      @buf: host memory buffer
 *      @dir: direction of transfer 1 => read, 0 => write
 *
 *      Reads/writes an [almost] arbitrary memory region in the firmware: the
 *      firmware memory address, length and host buffer must be aligned on
 *      32-bit boudaries.  The memory is transferred as a raw byte sequence
 *      from/to the firmware's memory.  If this memory contains data
 *      structures which contain multi-byte integers, it's the callers
 *      responsibility to perform appropriate byte order conversions.
 */
static int
csio_memory_rw(struct csio_hw *hw, int mtype, u32 addr, u32 len,
		uint32_t *buf, int dir)
{
	uint32_t pos, start, end, offset, memoffset;
	int ret;
	uint32_t *data;

	/*
	 * Argument sanity checks ...
	 */
	if ((addr & 0x3) || (len & 0x3))
		return -EINVAL;

	data = kzalloc(MEMWIN0_APERTURE, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	/* Offset into the region of memory which is being accessed
	 * MEM_EDC0 = 0
	 * MEM_EDC1 = 1
	 * MEM_MC   = 2
	 */
	memoffset = (mtype * (5 * 1024 * 1024));

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
				ret = csio_mem_win_rw(hw, pos, data, 1);
				if (ret) {
					kfree(data);
					return ret;
				}
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
		ret = csio_mem_win_rw(hw, pos, data, dir);
		if (ret) {
			kfree(data);
			return ret;
		}

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

	kfree(data);

	return 0;
}

static int
csio_memory_write(struct csio_hw *hw, int mtype, u32 addr, u32 len, u32 *buf)
{
	return csio_memory_rw(hw, mtype, addr, len, buf, 0);
}

/*
 * EEPROM reads take a few tens of us while writes can take a bit over 5 ms.
 */
#define EEPROM_MAX_RD_POLL 40
#define EEPROM_MAX_WR_POLL 6
#define EEPROM_STAT_ADDR   0x7bfc
#define VPD_BASE           0x400
#define VPD_BASE_OLD	   0
#define VPD_LEN            512
#define VPD_INFO_FLD_HDR_SIZE	3

/*
 *	csio_hw_seeprom_read - read a serial EEPROM location
 *	@hw: hw to read
 *	@addr: EEPROM virtual address
 *	@data: where to store the read data
 *
 *	Read a 32-bit word from a location in serial EEPROM using the card's PCI
 *	VPD capability.  Note that this function must be called with a virtual
 *	address.
 */
static int
csio_hw_seeprom_read(struct csio_hw *hw, uint32_t addr, uint32_t *data)
{
	uint16_t val = 0;
	int attempts = EEPROM_MAX_RD_POLL;
	uint32_t base = hw->params.pci.vpd_cap_addr;

	if (addr >= EEPROMVSIZE || (addr & 3))
		return -EINVAL;

	pci_write_config_word(hw->pdev, base + PCI_VPD_ADDR, (uint16_t)addr);

	do {
		udelay(10);
		pci_read_config_word(hw->pdev, base + PCI_VPD_ADDR, &val);
	} while (!(val & PCI_VPD_ADDR_F) && --attempts);

	if (!(val & PCI_VPD_ADDR_F)) {
		csio_err(hw, "reading EEPROM address 0x%x failed\n", addr);
		return -EINVAL;
	}

	pci_read_config_dword(hw->pdev, base + PCI_VPD_DATA, data);
	*data = le32_to_cpu(*data);

	return 0;
}

/*
 * Partial EEPROM Vital Product Data structure.  Includes only the ID and
 * VPD-R sections.
 */
struct t4_vpd_hdr {
	u8  id_tag;
	u8  id_len[2];
	u8  id_data[ID_LEN];
	u8  vpdr_tag;
	u8  vpdr_len[2];
};

/*
 *	csio_hw_get_vpd_keyword_val - Locates an information field keyword in
 *				      the VPD
 *	@v: Pointer to buffered vpd data structure
 *	@kw: The keyword to search for
 *
 *	Returns the value of the information field keyword or
 *	-EINVAL otherwise.
 */
static int
csio_hw_get_vpd_keyword_val(const struct t4_vpd_hdr *v, const char *kw)
{
	int32_t i;
	int32_t offset , len;
	const uint8_t *buf = &v->id_tag;
	const uint8_t *vpdr_len = &v->vpdr_tag;
	offset = sizeof(struct t4_vpd_hdr);
	len =  (uint16_t)vpdr_len[1] + ((uint16_t)vpdr_len[2] << 8);

	if (len + sizeof(struct t4_vpd_hdr) > VPD_LEN)
		return -EINVAL;

	for (i = offset; (i + VPD_INFO_FLD_HDR_SIZE) <= (offset + len);) {
		if (memcmp(buf + i , kw, 2) == 0) {
			i += VPD_INFO_FLD_HDR_SIZE;
			return i;
		}

		i += VPD_INFO_FLD_HDR_SIZE + buf[i+2];
	}

	return -EINVAL;
}

static int
csio_pci_capability(struct pci_dev *pdev, int cap, int *pos)
{
	*pos = pci_find_capability(pdev, cap);
	if (*pos)
		return 0;

	return -1;
}

/*
 *	csio_hw_get_vpd_params - read VPD parameters from VPD EEPROM
 *	@hw: HW module
 *	@p: where to store the parameters
 *
 *	Reads card parameters stored in VPD EEPROM.
 */
static int
csio_hw_get_vpd_params(struct csio_hw *hw, struct csio_vpd *p)
{
	int i, ret, ec, sn, addr;
	uint8_t *vpd, csum;
	const struct t4_vpd_hdr *v;
	/* To get around compilation warning from strstrip */
	char *s;

	if (csio_is_valid_vpd(hw))
		return 0;

	ret = csio_pci_capability(hw->pdev, PCI_CAP_ID_VPD,
				  &hw->params.pci.vpd_cap_addr);
	if (ret)
		return -EINVAL;

	vpd = kzalloc(VPD_LEN, GFP_ATOMIC);
	if (vpd == NULL)
		return -ENOMEM;

	/*
	 * Card information normally starts at VPD_BASE but early cards had
	 * it at 0.
	 */
	ret = csio_hw_seeprom_read(hw, VPD_BASE, (uint32_t *)(vpd));
	addr = *vpd == 0x82 ? VPD_BASE : VPD_BASE_OLD;

	for (i = 0; i < VPD_LEN; i += 4) {
		ret = csio_hw_seeprom_read(hw, addr + i, (uint32_t *)(vpd + i));
		if (ret) {
			kfree(vpd);
			return ret;
		}
	}

	/* Reset the VPD flag! */
	hw->flags &= (~CSIO_HWF_VPD_VALID);

	v = (const struct t4_vpd_hdr *)vpd;

#define FIND_VPD_KW(var, name) do { \
	var = csio_hw_get_vpd_keyword_val(v, name); \
	if (var < 0) { \
		csio_err(hw, "missing VPD keyword " name "\n"); \
		kfree(vpd); \
		return -EINVAL; \
	} \
} while (0)

	FIND_VPD_KW(i, "RV");
	for (csum = 0; i >= 0; i--)
		csum += vpd[i];

	if (csum) {
		csio_err(hw, "corrupted VPD EEPROM, actual csum %u\n", csum);
		kfree(vpd);
		return -EINVAL;
	}
	FIND_VPD_KW(ec, "EC");
	FIND_VPD_KW(sn, "SN");
#undef FIND_VPD_KW

	memcpy(p->id, v->id_data, ID_LEN);
	s = strstrip(p->id);
	memcpy(p->ec, vpd + ec, EC_LEN);
	s = strstrip(p->ec);
	i = vpd[sn - VPD_INFO_FLD_HDR_SIZE + 2];
	memcpy(p->sn, vpd + sn, min(i, SERNUM_LEN));
	s = strstrip(p->sn);

	csio_valid_vpd_copied(hw);

	kfree(vpd);
	return 0;
}

/*
 *	csio_hw_sf1_read - read data from the serial flash
 *	@hw: the HW module
 *	@byte_cnt: number of bytes to read
 *	@cont: whether another operation will be chained
 *      @lock: whether to lock SF for PL access only
 *	@valp: where to store the read data
 *
 *	Reads up to 4 bytes of data from the serial flash.  The location of
 *	the read needs to be specified prior to calling this by issuing the
 *	appropriate commands to the serial flash.
 */
static int
csio_hw_sf1_read(struct csio_hw *hw, uint32_t byte_cnt, int32_t cont,
		 int32_t lock, uint32_t *valp)
{
	int ret;

	if (!byte_cnt || byte_cnt > 4)
		return -EINVAL;
	if (csio_rd_reg32(hw, SF_OP) & SF_BUSY)
		return -EBUSY;

	cont = cont ? SF_CONT : 0;
	lock = lock ? SF_LOCK : 0;

	csio_wr_reg32(hw, lock | cont | BYTECNT(byte_cnt - 1), SF_OP);
	ret = csio_hw_wait_op_done_val(hw, SF_OP, SF_BUSY, 0, SF_ATTEMPTS,
					 10, NULL);
	if (!ret)
		*valp = csio_rd_reg32(hw, SF_DATA);
	return ret;
}

/*
 *	csio_hw_sf1_write - write data to the serial flash
 *	@hw: the HW module
 *	@byte_cnt: number of bytes to write
 *	@cont: whether another operation will be chained
 *      @lock: whether to lock SF for PL access only
 *	@val: value to write
 *
 *	Writes up to 4 bytes of data to the serial flash.  The location of
 *	the write needs to be specified prior to calling this by issuing the
 *	appropriate commands to the serial flash.
 */
static int
csio_hw_sf1_write(struct csio_hw *hw, uint32_t byte_cnt, uint32_t cont,
		  int32_t lock, uint32_t val)
{
	if (!byte_cnt || byte_cnt > 4)
		return -EINVAL;
	if (csio_rd_reg32(hw, SF_OP) & SF_BUSY)
		return -EBUSY;

	cont = cont ? SF_CONT : 0;
	lock = lock ? SF_LOCK : 0;

	csio_wr_reg32(hw, val, SF_DATA);
	csio_wr_reg32(hw, cont | BYTECNT(byte_cnt - 1) | OP_WR | lock, SF_OP);

	return csio_hw_wait_op_done_val(hw, SF_OP, SF_BUSY, 0, SF_ATTEMPTS,
					10, NULL);
}

/*
 *	csio_hw_flash_wait_op - wait for a flash operation to complete
 *	@hw: the HW module
 *	@attempts: max number of polls of the status register
 *	@delay: delay between polls in ms
 *
 *	Wait for a flash operation to complete by polling the status register.
 */
static int
csio_hw_flash_wait_op(struct csio_hw *hw, int32_t attempts, int32_t delay)
{
	int ret;
	uint32_t status;

	while (1) {
		ret = csio_hw_sf1_write(hw, 1, 1, 1, SF_RD_STATUS);
		if (ret != 0)
			return ret;

		ret = csio_hw_sf1_read(hw, 1, 0, 1, &status);
		if (ret != 0)
			return ret;

		if (!(status & 1))
			return 0;
		if (--attempts == 0)
			return -EAGAIN;
		if (delay)
			msleep(delay);
	}
}

/*
 *	csio_hw_read_flash - read words from serial flash
 *	@hw: the HW module
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
static int
csio_hw_read_flash(struct csio_hw *hw, uint32_t addr, uint32_t nwords,
		  uint32_t *data, int32_t byte_oriented)
{
	int ret;

	if (addr + nwords * sizeof(uint32_t) > hw->params.sf_size || (addr & 3))
		return -EINVAL;

	addr = swab32(addr) | SF_RD_DATA_FAST;

	ret = csio_hw_sf1_write(hw, 4, 1, 0, addr);
	if (ret != 0)
		return ret;

	ret = csio_hw_sf1_read(hw, 1, 1, 0, data);
	if (ret != 0)
		return ret;

	for ( ; nwords; nwords--, data++) {
		ret = csio_hw_sf1_read(hw, 4, nwords > 1, nwords == 1, data);
		if (nwords == 1)
			csio_wr_reg32(hw, 0, SF_OP);    /* unlock SF */
		if (ret)
			return ret;
		if (byte_oriented)
			*data = htonl(*data);
	}
	return 0;
}

/*
 *	csio_hw_write_flash - write up to a page of data to the serial flash
 *	@hw: the hw
 *	@addr: the start address to write
 *	@n: length of data to write in bytes
 *	@data: the data to write
 *
 *	Writes up to a page of data (256 bytes) to the serial flash starting
 *	at the given address.  All the data must be written to the same page.
 */
static int
csio_hw_write_flash(struct csio_hw *hw, uint32_t addr,
		    uint32_t n, const uint8_t *data)
{
	int ret = -EINVAL;
	uint32_t buf[64];
	uint32_t i, c, left, val, offset = addr & 0xff;

	if (addr >= hw->params.sf_size || offset + n > SF_PAGE_SIZE)
		return -EINVAL;

	val = swab32(addr) | SF_PROG_PAGE;

	ret = csio_hw_sf1_write(hw, 1, 0, 1, SF_WR_ENABLE);
	if (ret != 0)
		goto unlock;

	ret = csio_hw_sf1_write(hw, 4, 1, 1, val);
	if (ret != 0)
		goto unlock;

	for (left = n; left; left -= c) {
		c = min(left, 4U);
		for (val = 0, i = 0; i < c; ++i)
			val = (val << 8) + *data++;

		ret = csio_hw_sf1_write(hw, c, c != left, 1, val);
		if (ret)
			goto unlock;
	}
	ret = csio_hw_flash_wait_op(hw, 8, 1);
	if (ret)
		goto unlock;

	csio_wr_reg32(hw, 0, SF_OP);    /* unlock SF */

	/* Read the page to verify the write succeeded */
	ret = csio_hw_read_flash(hw, addr & ~0xff, ARRAY_SIZE(buf), buf, 1);
	if (ret)
		return ret;

	if (memcmp(data - n, (uint8_t *)buf + offset, n)) {
		csio_err(hw,
			 "failed to correctly write the flash page at %#x\n",
			 addr);
		return -EINVAL;
	}

	return 0;

unlock:
	csio_wr_reg32(hw, 0, SF_OP);    /* unlock SF */
	return ret;
}

/*
 *	csio_hw_flash_erase_sectors - erase a range of flash sectors
 *	@hw: the HW module
 *	@start: the first sector to erase
 *	@end: the last sector to erase
 *
 *	Erases the sectors in the given inclusive range.
 */
static int
csio_hw_flash_erase_sectors(struct csio_hw *hw, int32_t start, int32_t end)
{
	int ret = 0;

	while (start <= end) {

		ret = csio_hw_sf1_write(hw, 1, 0, 1, SF_WR_ENABLE);
		if (ret != 0)
			goto out;

		ret = csio_hw_sf1_write(hw, 4, 0, 1,
					SF_ERASE_SECTOR | (start << 8));
		if (ret != 0)
			goto out;

		ret = csio_hw_flash_wait_op(hw, 14, 500);
		if (ret != 0)
			goto out;

		start++;
	}
out:
	if (ret)
		csio_err(hw, "erase of flash sector %d failed, error %d\n",
			 start, ret);
	csio_wr_reg32(hw, 0, SF_OP);    /* unlock SF */
	return 0;
}

/*
 *	csio_hw_flash_cfg_addr - return the address of the flash
 *				configuration file
 *	@hw: the HW module
 *
 *	Return the address within the flash where the Firmware Configuration
 *	File is stored.
 */
static unsigned int
csio_hw_flash_cfg_addr(struct csio_hw *hw)
{
	if (hw->params.sf_size == 0x100000)
		return FPGA_FLASH_CFG_OFFSET;
	else
		return FLASH_CFG_OFFSET;
}

static void
csio_hw_print_fw_version(struct csio_hw *hw, char *str)
{
	csio_info(hw, "%s: %u.%u.%u.%u\n", str,
		    FW_HDR_FW_VER_MAJOR_GET(hw->fwrev),
		    FW_HDR_FW_VER_MINOR_GET(hw->fwrev),
		    FW_HDR_FW_VER_MICRO_GET(hw->fwrev),
		    FW_HDR_FW_VER_BUILD_GET(hw->fwrev));
}

/*
 * csio_hw_get_fw_version - read the firmware version
 * @hw: HW module
 * @vers: where to place the version
 *
 * Reads the FW version from flash.
 */
static int
csio_hw_get_fw_version(struct csio_hw *hw, uint32_t *vers)
{
	return csio_hw_read_flash(hw, FW_IMG_START +
				  offsetof(struct fw_hdr, fw_ver), 1,
				  vers, 0);
}

/*
 *	csio_hw_get_tp_version - read the TP microcode version
 *	@hw: HW module
 *	@vers: where to place the version
 *
 *	Reads the TP microcode version from flash.
 */
static int
csio_hw_get_tp_version(struct csio_hw *hw, u32 *vers)
{
	return csio_hw_read_flash(hw, FLASH_FW_START +
			offsetof(struct fw_hdr, tp_microcode_ver), 1,
			vers, 0);
}

/*
 *	csio_hw_check_fw_version - check if the FW is compatible with
 *				   this driver
 *	@hw: HW module
 *
 *	Checks if an adapter's FW is compatible with the driver.  Returns 0
 *	if there's exact match, a negative error if the version could not be
 *	read or there's a major/minor version mismatch/minor.
 */
static int
csio_hw_check_fw_version(struct csio_hw *hw)
{
	int ret, major, minor, micro;

	ret = csio_hw_get_fw_version(hw, &hw->fwrev);
	if (!ret)
		ret = csio_hw_get_tp_version(hw, &hw->tp_vers);
	if (ret)
		return ret;

	major = FW_HDR_FW_VER_MAJOR_GET(hw->fwrev);
	minor = FW_HDR_FW_VER_MINOR_GET(hw->fwrev);
	micro = FW_HDR_FW_VER_MICRO_GET(hw->fwrev);

	if (major != FW_VERSION_MAJOR) {            /* major mismatch - fail */
		csio_err(hw, "card FW has major version %u, driver wants %u\n",
			 major, FW_VERSION_MAJOR);
		return -EINVAL;
	}

	if (minor == FW_VERSION_MINOR && micro == FW_VERSION_MICRO)
		return 0;        /* perfect match */

	/* Minor/micro version mismatch */
	return -EINVAL;
}

/*
 * csio_hw_fw_dload - download firmware.
 * @hw: HW module
 * @fw_data: firmware image to write.
 * @size: image size
 *
 * Write the supplied firmware image to the card's serial flash.
 */
static int
csio_hw_fw_dload(struct csio_hw *hw, uint8_t *fw_data, uint32_t size)
{
	uint32_t csum;
	int32_t addr;
	int ret;
	uint32_t i;
	uint8_t first_page[SF_PAGE_SIZE];
	const __be32 *p = (const __be32 *)fw_data;
	struct fw_hdr *hdr = (struct fw_hdr *)fw_data;
	uint32_t sf_sec_size;

	if ((!hw->params.sf_size) || (!hw->params.sf_nsec)) {
		csio_err(hw, "Serial Flash data invalid\n");
		return -EINVAL;
	}

	if (!size) {
		csio_err(hw, "FW image has no data\n");
		return -EINVAL;
	}

	if (size & 511) {
		csio_err(hw, "FW image size not multiple of 512 bytes\n");
		return -EINVAL;
	}

	if (ntohs(hdr->len512) * 512 != size) {
		csio_err(hw, "FW image size differs from size in FW header\n");
		return -EINVAL;
	}

	if (size > FW_MAX_SIZE) {
		csio_err(hw, "FW image too large, max is %u bytes\n",
			    FW_MAX_SIZE);
		return -EINVAL;
	}

	for (csum = 0, i = 0; i < size / sizeof(csum); i++)
		csum += ntohl(p[i]);

	if (csum != 0xffffffff) {
		csio_err(hw, "corrupted firmware image, checksum %#x\n", csum);
		return -EINVAL;
	}

	sf_sec_size = hw->params.sf_size / hw->params.sf_nsec;
	i = DIV_ROUND_UP(size, sf_sec_size);        /* # of sectors spanned */

	csio_dbg(hw, "Erasing sectors... start:%d end:%d\n",
			  FW_START_SEC, FW_START_SEC + i - 1);

	ret = csio_hw_flash_erase_sectors(hw, FW_START_SEC,
					  FW_START_SEC + i - 1);
	if (ret) {
		csio_err(hw, "Flash Erase failed\n");
		goto out;
	}

	/*
	 * We write the correct version at the end so the driver can see a bad
	 * version if the FW write fails.  Start by writing a copy of the
	 * first page with a bad version.
	 */
	memcpy(first_page, fw_data, SF_PAGE_SIZE);
	((struct fw_hdr *)first_page)->fw_ver = htonl(0xffffffff);
	ret = csio_hw_write_flash(hw, FW_IMG_START, SF_PAGE_SIZE, first_page);
	if (ret)
		goto out;

	csio_dbg(hw, "Writing Flash .. start:%d end:%d\n",
		    FW_IMG_START, FW_IMG_START + size);

	addr = FW_IMG_START;
	for (size -= SF_PAGE_SIZE; size; size -= SF_PAGE_SIZE) {
		addr += SF_PAGE_SIZE;
		fw_data += SF_PAGE_SIZE;
		ret = csio_hw_write_flash(hw, addr, SF_PAGE_SIZE, fw_data);
		if (ret)
			goto out;
	}

	ret = csio_hw_write_flash(hw,
				  FW_IMG_START +
					offsetof(struct fw_hdr, fw_ver),
				  sizeof(hdr->fw_ver),
				  (const uint8_t *)&hdr->fw_ver);

out:
	if (ret)
		csio_err(hw, "firmware download failed, error %d\n", ret);
	return ret;
}

static int
csio_hw_get_flash_params(struct csio_hw *hw)
{
	int ret;
	uint32_t info = 0;

	ret = csio_hw_sf1_write(hw, 1, 1, 0, SF_RD_ID);
	if (!ret)
		ret = csio_hw_sf1_read(hw, 3, 0, 1, &info);
	csio_wr_reg32(hw, 0, SF_OP);    /* unlock SF */
	if (ret != 0)
		return ret;

	if ((info & 0xff) != 0x20)		/* not a Numonix flash */
		return -EINVAL;
	info >>= 16;				/* log2 of size */
	if (info >= 0x14 && info < 0x18)
		hw->params.sf_nsec = 1 << (info - 16);
	else if (info == 0x18)
		hw->params.sf_nsec = 64;
	else
		return -EINVAL;
	hw->params.sf_size = 1 << info;

	return 0;
}

static void
csio_set_pcie_completion_timeout(struct csio_hw *hw, u8 range)
{
	uint16_t val;
	uint32_t pcie_cap;

	if (!csio_pci_capability(hw->pdev, PCI_CAP_ID_EXP, &pcie_cap)) {
		pci_read_config_word(hw->pdev,
				     pcie_cap + PCI_EXP_DEVCTL2, &val);
		val &= 0xfff0;
		val |= range ;
		pci_write_config_word(hw->pdev,
				      pcie_cap + PCI_EXP_DEVCTL2, val);
	}
}


/*
 * Return the specified PCI-E Configuration Space register from our Physical
 * Function.  We try first via a Firmware LDST Command since we prefer to let
 * the firmware own all of these registers, but if that fails we go for it
 * directly ourselves.
 */
static uint32_t
csio_read_pcie_cfg4(struct csio_hw *hw, int reg)
{
	u32 val = 0;
	struct csio_mb *mbp;
	int rv;
	struct fw_ldst_cmd *ldst_cmd;

	mbp = mempool_alloc(hw->mb_mempool, GFP_ATOMIC);
	if (!mbp) {
		CSIO_INC_STATS(hw, n_err_nomem);
		pci_read_config_dword(hw->pdev, reg, &val);
		return val;
	}

	csio_mb_ldst(hw, mbp, CSIO_MB_DEFAULT_TMO, reg);

	rv = csio_mb_issue(hw, mbp);

	/*
	 * If the LDST Command suucceeded, exctract the returned register
	 * value.  Otherwise read it directly ourself.
	 */
	if (rv == 0) {
		ldst_cmd = (struct fw_ldst_cmd *)(mbp->mb);
		val = ntohl(ldst_cmd->u.pcie.data[0]);
	} else
		pci_read_config_dword(hw->pdev, reg, &val);

	mempool_free(mbp, hw->mb_mempool);

	return val;
} /* csio_read_pcie_cfg4 */

static int
csio_hw_set_mem_win(struct csio_hw *hw)
{
	u32 bar0;

	/*
	 * Truncation intentional: we only read the bottom 32-bits of the
	 * 64-bit BAR0/BAR1 ...  We use the hardware backdoor mechanism to
	 * read BAR0 instead of using pci_resource_start() because we could be
	 * operating from within a Virtual Machine which is trapping our
	 * accesses to our Configuration Space and we need to set up the PCI-E
	 * Memory Window decoders with the actual addresses which will be
	 * coming across the PCI-E link.
	 */
	bar0 = csio_read_pcie_cfg4(hw, PCI_BASE_ADDRESS_0);
	bar0 &= PCI_BASE_ADDRESS_MEM_MASK;

	/*
	 * Set up memory window for accessing adapter memory ranges.  (Read
	 * back MA register to ensure that changes propagate before we attempt
	 * to use the new values.)
	 */
	csio_wr_reg32(hw, (bar0 + MEMWIN0_BASE) | BIR(0) |
		WINDOW(ilog2(MEMWIN0_APERTURE) - 10),
		PCIE_MEM_ACCESS_REG(PCIE_MEM_ACCESS_BASE_WIN, 0));
	csio_wr_reg32(hw, (bar0 + MEMWIN1_BASE) | BIR(0) |
		WINDOW(ilog2(MEMWIN1_APERTURE) - 10),
		PCIE_MEM_ACCESS_REG(PCIE_MEM_ACCESS_BASE_WIN, 1));
	csio_wr_reg32(hw, (bar0 + MEMWIN2_BASE) | BIR(0) |
		WINDOW(ilog2(MEMWIN2_APERTURE) - 10),
		PCIE_MEM_ACCESS_REG(PCIE_MEM_ACCESS_BASE_WIN, 2));
	csio_rd_reg32(hw, PCIE_MEM_ACCESS_REG(PCIE_MEM_ACCESS_BASE_WIN, 2));
	return 0;
} /* csio_hw_set_mem_win */



/*****************************************************************************/
/* HW State machine assists                                                  */
/*****************************************************************************/

static int
csio_hw_dev_ready(struct csio_hw *hw)
{
	uint32_t reg;
	int cnt = 6;

	while (((reg = csio_rd_reg32(hw, PL_WHOAMI)) == 0xFFFFFFFF) &&
								(--cnt != 0))
		mdelay(100);

	if ((cnt == 0) && (((int32_t)(SOURCEPF_GET(reg)) < 0) ||
			    (SOURCEPF_GET(reg) >= CSIO_MAX_PFN))) {
		csio_err(hw, "PL_WHOAMI returned 0x%x, cnt:%d\n", reg, cnt);
		return -EIO;
	}

	hw->pfn = SOURCEPF_GET(reg);

	return 0;
}

/*
 * csio_do_hello - Perform the HELLO FW Mailbox command and process response.
 * @hw: HW module
 * @state: Device state
 *
 * FW_HELLO_CMD has to be polled for completion.
 */
static int
csio_do_hello(struct csio_hw *hw, enum csio_dev_state *state)
{
	struct csio_mb	*mbp;
	int	rv = 0;
	enum csio_dev_master master;
	enum fw_retval retval;
	uint8_t mpfn;
	char state_str[16];
	int retries = FW_CMD_HELLO_RETRIES;

	memset(state_str, 0, sizeof(state_str));

	mbp = mempool_alloc(hw->mb_mempool, GFP_ATOMIC);
	if (!mbp) {
		rv = -ENOMEM;
		CSIO_INC_STATS(hw, n_err_nomem);
		goto out;
	}

	master = csio_force_master ? CSIO_MASTER_MUST : CSIO_MASTER_MAY;

retry:
	csio_mb_hello(hw, mbp, CSIO_MB_DEFAULT_TMO, hw->pfn,
		      hw->pfn, master, NULL);

	rv = csio_mb_issue(hw, mbp);
	if (rv) {
		csio_err(hw, "failed to issue HELLO cmd. ret:%d.\n", rv);
		goto out_free_mb;
	}

	csio_mb_process_hello_rsp(hw, mbp, &retval, state, &mpfn);
	if (retval != FW_SUCCESS) {
		csio_err(hw, "HELLO cmd failed with ret: %d\n", retval);
		rv = -EINVAL;
		goto out_free_mb;
	}

	/* Firmware has designated us to be master */
	if (hw->pfn == mpfn) {
		hw->flags |= CSIO_HWF_MASTER;
	} else if (*state == CSIO_DEV_STATE_UNINIT) {
		/*
		 * If we're not the Master PF then we need to wait around for
		 * the Master PF Driver to finish setting up the adapter.
		 *
		 * Note that we also do this wait if we're a non-Master-capable
		 * PF and there is no current Master PF; a Master PF may show up
		 * momentarily and we wouldn't want to fail pointlessly.  (This
		 * can happen when an OS loads lots of different drivers rapidly
		 * at the same time). In this case, the Master PF returned by
		 * the firmware will be PCIE_FW_MASTER_MASK so the test below
		 * will work ...
		 */

		int waiting = FW_CMD_HELLO_TIMEOUT;

		/*
		 * Wait for the firmware to either indicate an error or
		 * initialized state.  If we see either of these we bail out
		 * and report the issue to the caller.  If we exhaust the
		 * "hello timeout" and we haven't exhausted our retries, try
		 * again.  Otherwise bail with a timeout error.
		 */
		for (;;) {
			uint32_t pcie_fw;

			msleep(50);
			waiting -= 50;

			/*
			 * If neither Error nor Initialialized are indicated
			 * by the firmware keep waiting till we exaust our
			 * timeout ... and then retry if we haven't exhausted
			 * our retries ...
			 */
			pcie_fw = csio_rd_reg32(hw, PCIE_FW);
			if (!(pcie_fw & (PCIE_FW_ERR|PCIE_FW_INIT))) {
				if (waiting <= 0) {
					if (retries-- > 0)
						goto retry;

					rv = -ETIMEDOUT;
					break;
				}
				continue;
			}

			/*
			 * We either have an Error or Initialized condition
			 * report errors preferentially.
			 */
			if (state) {
				if (pcie_fw & PCIE_FW_ERR) {
					*state = CSIO_DEV_STATE_ERR;
					rv = -ETIMEDOUT;
				} else if (pcie_fw & PCIE_FW_INIT)
					*state = CSIO_DEV_STATE_INIT;
			}

			/*
			 * If we arrived before a Master PF was selected and
			 * there's not a valid Master PF, grab its identity
			 * for our caller.
			 */
			if (mpfn == PCIE_FW_MASTER_MASK &&
			    (pcie_fw & PCIE_FW_MASTER_VLD))
				mpfn = PCIE_FW_MASTER_GET(pcie_fw);
			break;
		}
		hw->flags &= ~CSIO_HWF_MASTER;
	}

	switch (*state) {
	case CSIO_DEV_STATE_UNINIT:
		strcpy(state_str, "Initializing");
		break;
	case CSIO_DEV_STATE_INIT:
		strcpy(state_str, "Initialized");
		break;
	case CSIO_DEV_STATE_ERR:
		strcpy(state_str, "Error");
		break;
	default:
		strcpy(state_str, "Unknown");
		break;
	}

	if (hw->pfn == mpfn)
		csio_info(hw, "PF: %d, Coming up as MASTER, HW state: %s\n",
			hw->pfn, state_str);
	else
		csio_info(hw,
		    "PF: %d, Coming up as SLAVE, Master PF: %d, HW state: %s\n",
		    hw->pfn, mpfn, state_str);

out_free_mb:
	mempool_free(mbp, hw->mb_mempool);
out:
	return rv;
}

/*
 * csio_do_bye - Perform the BYE FW Mailbox command and process response.
 * @hw: HW module
 *
 */
static int
csio_do_bye(struct csio_hw *hw)
{
	struct csio_mb	*mbp;
	enum fw_retval retval;

	mbp = mempool_alloc(hw->mb_mempool, GFP_ATOMIC);
	if (!mbp) {
		CSIO_INC_STATS(hw, n_err_nomem);
		return -ENOMEM;
	}

	csio_mb_bye(hw, mbp, CSIO_MB_DEFAULT_TMO, NULL);

	if (csio_mb_issue(hw, mbp)) {
		csio_err(hw, "Issue of BYE command failed\n");
		mempool_free(mbp, hw->mb_mempool);
		return -EINVAL;
	}

	retval = csio_mb_fw_retval(mbp);
	if (retval != FW_SUCCESS) {
		mempool_free(mbp, hw->mb_mempool);
		return -EINVAL;
	}

	mempool_free(mbp, hw->mb_mempool);

	return 0;
}

/*
 * csio_do_reset- Perform the device reset.
 * @hw: HW module
 * @fw_rst: FW reset
 *
 * If fw_rst is set, issues FW reset mbox cmd otherwise
 * does PIO reset.
 * Performs reset of the function.
 */
static int
csio_do_reset(struct csio_hw *hw, bool fw_rst)
{
	struct csio_mb	*mbp;
	enum fw_retval retval;

	if (!fw_rst) {
		/* PIO reset */
		csio_wr_reg32(hw, PIORSTMODE | PIORST, PL_RST);
		mdelay(2000);
		return 0;
	}

	mbp = mempool_alloc(hw->mb_mempool, GFP_ATOMIC);
	if (!mbp) {
		CSIO_INC_STATS(hw, n_err_nomem);
		return -ENOMEM;
	}

	csio_mb_reset(hw, mbp, CSIO_MB_DEFAULT_TMO,
		      PIORSTMODE | PIORST, 0, NULL);

	if (csio_mb_issue(hw, mbp)) {
		csio_err(hw, "Issue of RESET command failed.n");
		mempool_free(mbp, hw->mb_mempool);
		return -EINVAL;
	}

	retval = csio_mb_fw_retval(mbp);
	if (retval != FW_SUCCESS) {
		csio_err(hw, "RESET cmd failed with ret:0x%x.\n", retval);
		mempool_free(mbp, hw->mb_mempool);
		return -EINVAL;
	}

	mempool_free(mbp, hw->mb_mempool);

	return 0;
}

static int
csio_hw_validate_caps(struct csio_hw *hw, struct csio_mb *mbp)
{
	struct fw_caps_config_cmd *rsp = (struct fw_caps_config_cmd *)mbp->mb;
	uint16_t caps;

	caps = ntohs(rsp->fcoecaps);

	if (!(caps & FW_CAPS_CONFIG_FCOE_INITIATOR)) {
		csio_err(hw, "No FCoE Initiator capability in the firmware.\n");
		return -EINVAL;
	}

	if (!(caps & FW_CAPS_CONFIG_FCOE_CTRL_OFLD)) {
		csio_err(hw, "No FCoE Control Offload capability\n");
		return -EINVAL;
	}

	return 0;
}

/*
 *	csio_hw_fw_halt - issue a reset/halt to FW and put uP into RESET
 *	@hw: the HW module
 *	@mbox: mailbox to use for the FW RESET command (if desired)
 *	@force: force uP into RESET even if FW RESET command fails
 *
 *	Issues a RESET command to firmware (if desired) with a HALT indication
 *	and then puts the microprocessor into RESET state.  The RESET command
 *	will only be issued if a legitimate mailbox is provided (mbox <=
 *	PCIE_FW_MASTER_MASK).
 *
 *	This is generally used in order for the host to safely manipulate the
 *	adapter without fear of conflicting with whatever the firmware might
 *	be doing.  The only way out of this state is to RESTART the firmware
 *	...
 */
static int
csio_hw_fw_halt(struct csio_hw *hw, uint32_t mbox, int32_t force)
{
	enum fw_retval retval = 0;

	/*
	 * If a legitimate mailbox is provided, issue a RESET command
	 * with a HALT indication.
	 */
	if (mbox <= PCIE_FW_MASTER_MASK) {
		struct csio_mb	*mbp;

		mbp = mempool_alloc(hw->mb_mempool, GFP_ATOMIC);
		if (!mbp) {
			CSIO_INC_STATS(hw, n_err_nomem);
			return -ENOMEM;
		}

		csio_mb_reset(hw, mbp, CSIO_MB_DEFAULT_TMO,
			      PIORSTMODE | PIORST, FW_RESET_CMD_HALT(1),
			      NULL);

		if (csio_mb_issue(hw, mbp)) {
			csio_err(hw, "Issue of RESET command failed!\n");
			mempool_free(mbp, hw->mb_mempool);
			return -EINVAL;
		}

		retval = csio_mb_fw_retval(mbp);
		mempool_free(mbp, hw->mb_mempool);
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
	if (retval == 0 || force) {
		csio_set_reg_field(hw, CIM_BOOT_CFG, UPCRST, UPCRST);
		csio_set_reg_field(hw, PCIE_FW, PCIE_FW_HALT, PCIE_FW_HALT);
	}

	/*
	 * And we always return the result of the firmware RESET command
	 * even when we force the uP into RESET ...
	 */
	return retval ? -EINVAL : 0;
}

/*
 *	csio_hw_fw_restart - restart the firmware by taking the uP out of RESET
 *	@hw: the HW module
 *	@reset: if we want to do a RESET to restart things
 *
 *	Restart firmware previously halted by csio_hw_fw_halt().  On successful
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
static int
csio_hw_fw_restart(struct csio_hw *hw, uint32_t mbox, int32_t reset)
{
	if (reset) {
		/*
		 * Since we're directing the RESET instead of the firmware
		 * doing it automatically, we need to clear the PCIE_FW.HALT
		 * bit.
		 */
		csio_set_reg_field(hw, PCIE_FW, PCIE_FW_HALT, 0);

		/*
		 * If we've been given a valid mailbox, first try to get the
		 * firmware to do the RESET.  If that works, great and we can
		 * return success.  Otherwise, if we haven't been given a
		 * valid mailbox or the RESET command failed, fall back to
		 * hitting the chip with a hammer.
		 */
		if (mbox <= PCIE_FW_MASTER_MASK) {
			csio_set_reg_field(hw, CIM_BOOT_CFG, UPCRST, 0);
			msleep(100);
			if (csio_do_reset(hw, true) == 0)
				return 0;
		}

		csio_wr_reg32(hw, PIORSTMODE | PIORST, PL_RST);
		msleep(2000);
	} else {
		int ms;

		csio_set_reg_field(hw, CIM_BOOT_CFG, UPCRST, 0);
		for (ms = 0; ms < FW_CMD_MAX_TIMEOUT; ) {
			if (!(csio_rd_reg32(hw, PCIE_FW) & PCIE_FW_HALT))
				return 0;
			msleep(100);
			ms += 100;
		}
		return -ETIMEDOUT;
	}
	return 0;
}

/*
 *	csio_hw_fw_upgrade - perform all of the steps necessary to upgrade FW
 *	@hw: the HW module
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
static int
csio_hw_fw_upgrade(struct csio_hw *hw, uint32_t mbox,
		  const u8 *fw_data, uint32_t size, int32_t force)
{
	const struct fw_hdr *fw_hdr = (const struct fw_hdr *)fw_data;
	int reset, ret;

	ret = csio_hw_fw_halt(hw, mbox, force);
	if (ret != 0 && !force)
		return ret;

	ret = csio_hw_fw_dload(hw, (uint8_t *) fw_data, size);
	if (ret != 0)
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
	return csio_hw_fw_restart(hw, mbox, reset);
}


/*
 *	csio_hw_fw_config_file - setup an adapter via a Configuration File
 *	@hw: the HW module
 *	@mbox: mailbox to use for the FW command
 *	@mtype: the memory type where the Configuration File is located
 *	@maddr: the memory address where the Configuration File is located
 *	@finiver: return value for CF [fini] version
 *	@finicsum: return value for CF [fini] checksum
 *	@cfcsum: return value for CF computed checksum
 *
 *	Issue a command to get the firmware to process the Configuration
 *	File located at the specified mtype/maddress.  If the Configuration
 *	File is processed successfully and return value pointers are
 *	provided, the Configuration File "[fini] section version and
 *	checksum values will be returned along with the computed checksum.
 *	It's up to the caller to decide how it wants to respond to the
 *	checksums not matching but it recommended that a prominant warning
 *	be emitted in order to help people rapidly identify changed or
 *	corrupted Configuration Files.
 *
 *	Also note that it's possible to modify things like "niccaps",
 *	"toecaps",etc. between processing the Configuration File and telling
 *	the firmware to use the new configuration.  Callers which want to
 *	do this will need to "hand-roll" their own CAPS_CONFIGS commands for
 *	Configuration Files if they want to do this.
 */
static int
csio_hw_fw_config_file(struct csio_hw *hw,
		      unsigned int mtype, unsigned int maddr,
		      uint32_t *finiver, uint32_t *finicsum, uint32_t *cfcsum)
{
	struct csio_mb	*mbp;
	struct fw_caps_config_cmd *caps_cmd;
	int rv = -EINVAL;
	enum fw_retval ret;

	mbp = mempool_alloc(hw->mb_mempool, GFP_ATOMIC);
	if (!mbp) {
		CSIO_INC_STATS(hw, n_err_nomem);
		return -ENOMEM;
	}
	/*
	 * Tell the firmware to process the indicated Configuration File.
	 * If there are no errors and the caller has provided return value
	 * pointers for the [fini] section version, checksum and computed
	 * checksum, pass those back to the caller.
	 */
	caps_cmd = (struct fw_caps_config_cmd *)(mbp->mb);
	CSIO_INIT_MBP(mbp, caps_cmd, CSIO_MB_DEFAULT_TMO, hw, NULL, 1);
	caps_cmd->op_to_write =
		htonl(FW_CMD_OP(FW_CAPS_CONFIG_CMD) |
		      FW_CMD_REQUEST |
		      FW_CMD_READ);
	caps_cmd->cfvalid_to_len16 =
		htonl(FW_CAPS_CONFIG_CMD_CFVALID |
		      FW_CAPS_CONFIG_CMD_MEMTYPE_CF(mtype) |
		      FW_CAPS_CONFIG_CMD_MEMADDR64K_CF(maddr >> 16) |
		      FW_LEN16(*caps_cmd));

	if (csio_mb_issue(hw, mbp)) {
		csio_err(hw, "Issue of FW_CAPS_CONFIG_CMD failed!\n");
		goto out;
	}

	ret = csio_mb_fw_retval(mbp);
	if (ret != FW_SUCCESS) {
		csio_dbg(hw, "FW_CAPS_CONFIG_CMD returned %d!\n", rv);
		goto out;
	}

	if (finiver)
		*finiver = ntohl(caps_cmd->finiver);
	if (finicsum)
		*finicsum = ntohl(caps_cmd->finicsum);
	if (cfcsum)
		*cfcsum = ntohl(caps_cmd->cfcsum);

	/* Validate device capabilities */
	if (csio_hw_validate_caps(hw, mbp)) {
		rv = -ENOENT;
		goto out;
	}

	/*
	 * And now tell the firmware to use the configuration we just loaded.
	 */
	caps_cmd->op_to_write =
		htonl(FW_CMD_OP(FW_CAPS_CONFIG_CMD) |
		      FW_CMD_REQUEST |
		      FW_CMD_WRITE);
	caps_cmd->cfvalid_to_len16 = htonl(FW_LEN16(*caps_cmd));

	if (csio_mb_issue(hw, mbp)) {
		csio_err(hw, "Issue of FW_CAPS_CONFIG_CMD failed!\n");
		goto out;
	}

	ret = csio_mb_fw_retval(mbp);
	if (ret != FW_SUCCESS) {
		csio_dbg(hw, "FW_CAPS_CONFIG_CMD returned %d!\n", rv);
		goto out;
	}

	rv = 0;
out:
	mempool_free(mbp, hw->mb_mempool);
	return rv;
}

/*
 * csio_get_device_params - Get device parameters.
 * @hw: HW module
 *
 */
static int
csio_get_device_params(struct csio_hw *hw)
{
	struct csio_wrm *wrm	= csio_hw_to_wrm(hw);
	struct csio_mb	*mbp;
	enum fw_retval retval;
	u32 param[6];
	int i, j = 0;

	/* Initialize portids to -1 */
	for (i = 0; i < CSIO_MAX_PPORTS; i++)
		hw->pport[i].portid = -1;

	mbp = mempool_alloc(hw->mb_mempool, GFP_ATOMIC);
	if (!mbp) {
		CSIO_INC_STATS(hw, n_err_nomem);
		return -ENOMEM;
	}

	/* Get port vec information. */
	param[0] = FW_PARAM_DEV(PORTVEC);

	/* Get Core clock. */
	param[1] = FW_PARAM_DEV(CCLK);

	/* Get EQ id start and end. */
	param[2] = FW_PARAM_PFVF(EQ_START);
	param[3] = FW_PARAM_PFVF(EQ_END);

	/* Get IQ id start and end. */
	param[4] = FW_PARAM_PFVF(IQFLINT_START);
	param[5] = FW_PARAM_PFVF(IQFLINT_END);

	csio_mb_params(hw, mbp, CSIO_MB_DEFAULT_TMO, hw->pfn, 0,
		       ARRAY_SIZE(param), param, NULL, false, NULL);
	if (csio_mb_issue(hw, mbp)) {
		csio_err(hw, "Issue of FW_PARAMS_CMD(read) failed!\n");
		mempool_free(mbp, hw->mb_mempool);
		return -EINVAL;
	}

	csio_mb_process_read_params_rsp(hw, mbp, &retval,
			ARRAY_SIZE(param), param);
	if (retval != FW_SUCCESS) {
		csio_err(hw, "FW_PARAMS_CMD(read) failed with ret:0x%x!\n",
				retval);
		mempool_free(mbp, hw->mb_mempool);
		return -EINVAL;
	}

	/* cache the information. */
	hw->port_vec = param[0];
	hw->vpd.cclk = param[1];
	wrm->fw_eq_start = param[2];
	wrm->fw_iq_start = param[4];

	/* Using FW configured max iqs & eqs */
	if ((hw->flags & CSIO_HWF_USING_SOFT_PARAMS) ||
		!csio_is_hw_master(hw)) {
		hw->cfg_niq = param[5] - param[4] + 1;
		hw->cfg_neq = param[3] - param[2] + 1;
		csio_dbg(hw, "Using fwconfig max niqs %d neqs %d\n",
			hw->cfg_niq, hw->cfg_neq);
	}

	hw->port_vec &= csio_port_mask;

	hw->num_pports	= hweight32(hw->port_vec);

	csio_dbg(hw, "Port vector: 0x%x, #ports: %d\n",
		    hw->port_vec, hw->num_pports);

	for (i = 0; i < hw->num_pports; i++) {
		while ((hw->port_vec & (1 << j)) == 0)
			j++;
		hw->pport[i].portid = j++;
		csio_dbg(hw, "Found Port:%d\n", hw->pport[i].portid);
	}
	mempool_free(mbp, hw->mb_mempool);

	return 0;
}


/*
 * csio_config_device_caps - Get and set device capabilities.
 * @hw: HW module
 *
 */
static int
csio_config_device_caps(struct csio_hw *hw)
{
	struct csio_mb	*mbp;
	enum fw_retval retval;
	int rv = -EINVAL;

	mbp = mempool_alloc(hw->mb_mempool, GFP_ATOMIC);
	if (!mbp) {
		CSIO_INC_STATS(hw, n_err_nomem);
		return -ENOMEM;
	}

	/* Get device capabilities */
	csio_mb_caps_config(hw, mbp, CSIO_MB_DEFAULT_TMO, 0, 0, 0, 0, NULL);

	if (csio_mb_issue(hw, mbp)) {
		csio_err(hw, "Issue of FW_CAPS_CONFIG_CMD(r) failed!\n");
		goto out;
	}

	retval = csio_mb_fw_retval(mbp);
	if (retval != FW_SUCCESS) {
		csio_err(hw, "FW_CAPS_CONFIG_CMD(r) returned %d!\n", retval);
		goto out;
	}

	/* Validate device capabilities */
	if (csio_hw_validate_caps(hw, mbp))
		goto out;

	/* Don't config device capabilities if already configured */
	if (hw->fw_state == CSIO_DEV_STATE_INIT) {
		rv = 0;
		goto out;
	}

	/* Write back desired device capabilities */
	csio_mb_caps_config(hw, mbp, CSIO_MB_DEFAULT_TMO, true, true,
			    false, true, NULL);

	if (csio_mb_issue(hw, mbp)) {
		csio_err(hw, "Issue of FW_CAPS_CONFIG_CMD(w) failed!\n");
		goto out;
	}

	retval = csio_mb_fw_retval(mbp);
	if (retval != FW_SUCCESS) {
		csio_err(hw, "FW_CAPS_CONFIG_CMD(w) returned %d!\n", retval);
		goto out;
	}

	rv = 0;
out:
	mempool_free(mbp, hw->mb_mempool);
	return rv;
}

static int
csio_config_global_rss(struct csio_hw *hw)
{
	struct csio_mb	*mbp;
	enum fw_retval retval;

	mbp = mempool_alloc(hw->mb_mempool, GFP_ATOMIC);
	if (!mbp) {
		CSIO_INC_STATS(hw, n_err_nomem);
		return -ENOMEM;
	}

	csio_rss_glb_config(hw, mbp, CSIO_MB_DEFAULT_TMO,
			    FW_RSS_GLB_CONFIG_CMD_MODE_BASICVIRTUAL,
			    FW_RSS_GLB_CONFIG_CMD_TNLMAPEN |
			    FW_RSS_GLB_CONFIG_CMD_HASHTOEPLITZ |
			    FW_RSS_GLB_CONFIG_CMD_TNLALLLKP,
			    NULL);

	if (csio_mb_issue(hw, mbp)) {
		csio_err(hw, "Issue of FW_RSS_GLB_CONFIG_CMD failed!\n");
		mempool_free(mbp, hw->mb_mempool);
		return -EINVAL;
	}

	retval = csio_mb_fw_retval(mbp);
	if (retval != FW_SUCCESS) {
		csio_err(hw, "FW_RSS_GLB_CONFIG_CMD returned 0x%x!\n", retval);
		mempool_free(mbp, hw->mb_mempool);
		return -EINVAL;
	}

	mempool_free(mbp, hw->mb_mempool);

	return 0;
}

/*
 * csio_config_pfvf - Configure Physical/Virtual functions settings.
 * @hw: HW module
 *
 */
static int
csio_config_pfvf(struct csio_hw *hw)
{
	struct csio_mb	*mbp;
	enum fw_retval retval;

	mbp = mempool_alloc(hw->mb_mempool, GFP_ATOMIC);
	if (!mbp) {
		CSIO_INC_STATS(hw, n_err_nomem);
		return -ENOMEM;
	}

	/*
	 * For now, allow all PFs to access to all ports using a pmask
	 * value of 0xF (M_FW_PFVF_CMD_PMASK). Once we have VFs, we will
	 * need to provide access based on some rule.
	 */
	csio_mb_pfvf(hw, mbp, CSIO_MB_DEFAULT_TMO, hw->pfn, 0, CSIO_NEQ,
		     CSIO_NETH_CTRL, CSIO_NIQ_FLINT, 0, 0, CSIO_NVI, CSIO_CMASK,
		     CSIO_PMASK, CSIO_NEXACTF, CSIO_R_CAPS, CSIO_WX_CAPS, NULL);

	if (csio_mb_issue(hw, mbp)) {
		csio_err(hw, "Issue of FW_PFVF_CMD failed!\n");
		mempool_free(mbp, hw->mb_mempool);
		return -EINVAL;
	}

	retval = csio_mb_fw_retval(mbp);
	if (retval != FW_SUCCESS) {
		csio_err(hw, "FW_PFVF_CMD returned 0x%x!\n", retval);
		mempool_free(mbp, hw->mb_mempool);
		return -EINVAL;
	}

	mempool_free(mbp, hw->mb_mempool);

	return 0;
}

/*
 * csio_enable_ports - Bring up all available ports.
 * @hw: HW module.
 *
 */
static int
csio_enable_ports(struct csio_hw *hw)
{
	struct csio_mb  *mbp;
	enum fw_retval retval;
	uint8_t portid;
	int i;

	mbp = mempool_alloc(hw->mb_mempool, GFP_ATOMIC);
	if (!mbp) {
		CSIO_INC_STATS(hw, n_err_nomem);
		return -ENOMEM;
	}

	for (i = 0; i < hw->num_pports; i++) {
		portid = hw->pport[i].portid;

		/* Read PORT information */
		csio_mb_port(hw, mbp, CSIO_MB_DEFAULT_TMO, portid,
			     false, 0, 0, NULL);

		if (csio_mb_issue(hw, mbp)) {
			csio_err(hw, "failed to issue FW_PORT_CMD(r) port:%d\n",
				 portid);
			mempool_free(mbp, hw->mb_mempool);
			return -EINVAL;
		}

		csio_mb_process_read_port_rsp(hw, mbp, &retval,
					      &hw->pport[i].pcap);
		if (retval != FW_SUCCESS) {
			csio_err(hw, "FW_PORT_CMD(r) port:%d failed: 0x%x\n",
				 portid, retval);
			mempool_free(mbp, hw->mb_mempool);
			return -EINVAL;
		}

		/* Write back PORT information */
		csio_mb_port(hw, mbp, CSIO_MB_DEFAULT_TMO, portid, true,
			     (PAUSE_RX | PAUSE_TX), hw->pport[i].pcap, NULL);

		if (csio_mb_issue(hw, mbp)) {
			csio_err(hw, "failed to issue FW_PORT_CMD(w) port:%d\n",
				 portid);
			mempool_free(mbp, hw->mb_mempool);
			return -EINVAL;
		}

		retval = csio_mb_fw_retval(mbp);
		if (retval != FW_SUCCESS) {
			csio_err(hw, "FW_PORT_CMD(w) port:%d failed :0x%x\n",
				 portid, retval);
			mempool_free(mbp, hw->mb_mempool);
			return -EINVAL;
		}

	} /* For all ports */

	mempool_free(mbp, hw->mb_mempool);

	return 0;
}

/*
 * csio_get_fcoe_resinfo - Read fcoe fw resource info.
 * @hw: HW module
 * Issued with lock held.
 */
static int
csio_get_fcoe_resinfo(struct csio_hw *hw)
{
	struct csio_fcoe_res_info *res_info = &hw->fres_info;
	struct fw_fcoe_res_info_cmd *rsp;
	struct csio_mb  *mbp;
	enum fw_retval retval;

	mbp = mempool_alloc(hw->mb_mempool, GFP_ATOMIC);
	if (!mbp) {
		CSIO_INC_STATS(hw, n_err_nomem);
		return -ENOMEM;
	}

	/* Get FCoE FW resource information */
	csio_fcoe_read_res_info_init_mb(hw, mbp, CSIO_MB_DEFAULT_TMO, NULL);

	if (csio_mb_issue(hw, mbp)) {
		csio_err(hw, "failed to issue FW_FCOE_RES_INFO_CMD\n");
		mempool_free(mbp, hw->mb_mempool);
		return -EINVAL;
	}

	rsp = (struct fw_fcoe_res_info_cmd *)(mbp->mb);
	retval = FW_CMD_RETVAL_GET(ntohl(rsp->retval_len16));
	if (retval != FW_SUCCESS) {
		csio_err(hw, "FW_FCOE_RES_INFO_CMD failed with ret x%x\n",
			 retval);
		mempool_free(mbp, hw->mb_mempool);
		return -EINVAL;
	}

	res_info->e_d_tov = ntohs(rsp->e_d_tov);
	res_info->r_a_tov_seq = ntohs(rsp->r_a_tov_seq);
	res_info->r_a_tov_els = ntohs(rsp->r_a_tov_els);
	res_info->r_r_tov = ntohs(rsp->r_r_tov);
	res_info->max_xchgs = ntohl(rsp->max_xchgs);
	res_info->max_ssns = ntohl(rsp->max_ssns);
	res_info->used_xchgs = ntohl(rsp->used_xchgs);
	res_info->used_ssns = ntohl(rsp->used_ssns);
	res_info->max_fcfs = ntohl(rsp->max_fcfs);
	res_info->max_vnps = ntohl(rsp->max_vnps);
	res_info->used_fcfs = ntohl(rsp->used_fcfs);
	res_info->used_vnps = ntohl(rsp->used_vnps);

	csio_dbg(hw, "max ssns:%d max xchgs:%d\n", res_info->max_ssns,
						  res_info->max_xchgs);
	mempool_free(mbp, hw->mb_mempool);

	return 0;
}

static int
csio_hw_check_fwconfig(struct csio_hw *hw, u32 *param)
{
	struct csio_mb	*mbp;
	enum fw_retval retval;
	u32 _param[1];

	mbp = mempool_alloc(hw->mb_mempool, GFP_ATOMIC);
	if (!mbp) {
		CSIO_INC_STATS(hw, n_err_nomem);
		return -ENOMEM;
	}

	/*
	 * Find out whether we're dealing with a version of
	 * the firmware which has configuration file support.
	 */
	_param[0] = (FW_PARAMS_MNEM(FW_PARAMS_MNEM_DEV) |
		     FW_PARAMS_PARAM_X(FW_PARAMS_PARAM_DEV_CF));

	csio_mb_params(hw, mbp, CSIO_MB_DEFAULT_TMO, hw->pfn, 0,
		       ARRAY_SIZE(_param), _param, NULL, false, NULL);
	if (csio_mb_issue(hw, mbp)) {
		csio_err(hw, "Issue of FW_PARAMS_CMD(read) failed!\n");
		mempool_free(mbp, hw->mb_mempool);
		return -EINVAL;
	}

	csio_mb_process_read_params_rsp(hw, mbp, &retval,
			ARRAY_SIZE(_param), _param);
	if (retval != FW_SUCCESS) {
		csio_err(hw, "FW_PARAMS_CMD(read) failed with ret:0x%x!\n",
				retval);
		mempool_free(mbp, hw->mb_mempool);
		return -EINVAL;
	}

	mempool_free(mbp, hw->mb_mempool);
	*param = _param[0];

	return 0;
}

static int
csio_hw_flash_config(struct csio_hw *hw, u32 *fw_cfg_param, char *path)
{
	int ret = 0;
	const struct firmware *cf;
	struct pci_dev *pci_dev = hw->pdev;
	struct device *dev = &pci_dev->dev;
	unsigned int mtype = 0, maddr = 0;
	uint32_t *cfg_data;
	int value_to_add = 0;

	if (request_firmware(&cf, CSIO_CF_FNAME, dev) < 0) {
		csio_err(hw, "could not find config file " CSIO_CF_FNAME
			 ",err: %d\n", ret);
		return -ENOENT;
	}

	if (cf->size%4 != 0)
		value_to_add = 4 - (cf->size % 4);

	cfg_data = kzalloc(cf->size+value_to_add, GFP_KERNEL);
	if (cfg_data == NULL) {
		ret = -ENOMEM;
		goto leave;
	}

	memcpy((void *)cfg_data, (const void *)cf->data, cf->size);
	if (csio_hw_check_fwconfig(hw, fw_cfg_param) != 0) {
		ret = -EINVAL;
		goto leave;
	}

	mtype = FW_PARAMS_PARAM_Y_GET(*fw_cfg_param);
	maddr = FW_PARAMS_PARAM_Z_GET(*fw_cfg_param) << 16;

	ret = csio_memory_write(hw, mtype, maddr,
				cf->size + value_to_add, cfg_data);
	if (ret == 0) {
		csio_info(hw, "config file upgraded to " CSIO_CF_FNAME "\n");
		strncpy(path, "/lib/firmware/" CSIO_CF_FNAME, 64);
	}

leave:
	kfree(cfg_data);
	release_firmware(cf);
	return ret;
}

/*
 * HW initialization: contact FW, obtain config, perform basic init.
 *
 * If the firmware we're dealing with has Configuration File support, then
 * we use that to perform all configuration -- either using the configuration
 * file stored in flash on the adapter or using a filesystem-local file
 * if available.
 *
 * If we don't have configuration file support in the firmware, then we'll
 * have to set things up the old fashioned way with hard-coded register
 * writes and firmware commands ...
 */

/*
 * Attempt to initialize the HW via a Firmware Configuration File.
 */
static int
csio_hw_use_fwconfig(struct csio_hw *hw, int reset, u32 *fw_cfg_param)
{
	unsigned int mtype, maddr;
	int rv;
	uint32_t finiver, finicsum, cfcsum;
	int using_flash;
	char path[64];

	/*
	 * Reset device if necessary
	 */
	if (reset) {
		rv = csio_do_reset(hw, true);
		if (rv != 0)
			goto bye;
	}

	/*
	 * If we have a configuration file in host ,
	 * then use that.  Otherwise, use the configuration file stored
	 * in the HW flash ...
	 */
	spin_unlock_irq(&hw->lock);
	rv = csio_hw_flash_config(hw, fw_cfg_param, path);
	spin_lock_irq(&hw->lock);
	if (rv != 0) {
		if (rv == -ENOENT) {
			/*
			 * config file was not found. Use default
			 * config file from flash.
			 */
			mtype = FW_MEMTYPE_CF_FLASH;
			maddr = csio_hw_flash_cfg_addr(hw);
			using_flash = 1;
		} else {
			/*
			 * we revert back to the hardwired config if
			 * flashing failed.
			 */
			goto bye;
		}
	} else {
		mtype = FW_PARAMS_PARAM_Y_GET(*fw_cfg_param);
		maddr = FW_PARAMS_PARAM_Z_GET(*fw_cfg_param) << 16;
		using_flash = 0;
	}

	hw->cfg_store = (uint8_t)mtype;

	/*
	 * Issue a Capability Configuration command to the firmware to get it
	 * to parse the Configuration File.
	 */
	rv = csio_hw_fw_config_file(hw, mtype, maddr, &finiver,
		&finicsum, &cfcsum);
	if (rv != 0)
		goto bye;

	hw->cfg_finiver		= finiver;
	hw->cfg_finicsum	= finicsum;
	hw->cfg_cfcsum		= cfcsum;
	hw->cfg_csum_status	= true;

	if (finicsum != cfcsum) {
		csio_warn(hw,
		      "Config File checksum mismatch: csum=%#x, computed=%#x\n",
		      finicsum, cfcsum);

		hw->cfg_csum_status = false;
	}

	/*
	 * Note that we're operating with parameters
	 * not supplied by the driver, rather than from hard-wired
	 * initialization constants buried in the driver.
	 */
	hw->flags |= CSIO_HWF_USING_SOFT_PARAMS;

	/* device parameters */
	rv = csio_get_device_params(hw);
	if (rv != 0)
		goto bye;

	/* Configure SGE */
	csio_wr_sge_init(hw);

	/*
	 * And finally tell the firmware to initialize itself using the
	 * parameters from the Configuration File.
	 */
	/* Post event to notify completion of configuration */
	csio_post_event(&hw->sm, CSIO_HWE_INIT);

	csio_info(hw,
	 "Firmware Configuration File %s, version %#x, computed checksum %#x\n",
		  (using_flash ? "in device FLASH" : path), finiver, cfcsum);

	return 0;

	/*
	 * Something bad happened.  Return the error ...
	 */
bye:
	hw->flags &= ~CSIO_HWF_USING_SOFT_PARAMS;
	csio_dbg(hw, "Configuration file error %d\n", rv);
	return rv;
}

/*
 * Attempt to initialize the adapter via hard-coded, driver supplied
 * parameters ...
 */
static int
csio_hw_no_fwconfig(struct csio_hw *hw, int reset)
{
	int		rv;
	/*
	 * Reset device if necessary
	 */
	if (reset) {
		rv = csio_do_reset(hw, true);
		if (rv != 0)
			goto out;
	}

	/* Get and set device capabilities */
	rv = csio_config_device_caps(hw);
	if (rv != 0)
		goto out;

	/* Config Global RSS command */
	rv = csio_config_global_rss(hw);
	if (rv != 0)
		goto out;

	/* Configure PF/VF capabilities of device */
	rv = csio_config_pfvf(hw);
	if (rv != 0)
		goto out;

	/* device parameters */
	rv = csio_get_device_params(hw);
	if (rv != 0)
		goto out;

	/* Configure SGE */
	csio_wr_sge_init(hw);

	/* Post event to notify completion of configuration */
	csio_post_event(&hw->sm, CSIO_HWE_INIT);

out:
	return rv;
}

/*
 * Returns -EINVAL if attempts to flash the firmware failed
 * else returns 0,
 * if flashing was not attempted because the card had the
 * latest firmware ECANCELED is returned
 */
static int
csio_hw_flash_fw(struct csio_hw *hw)
{
	int ret = -ECANCELED;
	const struct firmware *fw;
	const struct fw_hdr *hdr;
	u32 fw_ver;
	struct pci_dev *pci_dev = hw->pdev;
	struct device *dev = &pci_dev->dev ;

	if (request_firmware(&fw, CSIO_FW_FNAME, dev) < 0) {
		csio_err(hw, "could not find firmware image " CSIO_FW_FNAME
		",err: %d\n", ret);
		return -EINVAL;
	}

	hdr = (const struct fw_hdr *)fw->data;
	fw_ver = ntohl(hdr->fw_ver);
	if (FW_HDR_FW_VER_MAJOR_GET(fw_ver) != FW_VERSION_MAJOR)
		return -EINVAL;      /* wrong major version, won't do */

	/*
	 * If the flash FW is unusable or we found something newer, load it.
	 */
	if (FW_HDR_FW_VER_MAJOR_GET(hw->fwrev) != FW_VERSION_MAJOR ||
	    fw_ver > hw->fwrev) {
		ret = csio_hw_fw_upgrade(hw, hw->pfn, fw->data, fw->size,
				    /*force=*/false);
		if (!ret)
			csio_info(hw, "firmware upgraded to version %pI4 from "
				  CSIO_FW_FNAME "\n", &hdr->fw_ver);
		else
			csio_err(hw, "firmware upgrade failed! err=%d\n", ret);
	}

	release_firmware(fw);

	return ret;
}


/*
 * csio_hw_configure - Configure HW
 * @hw - HW module
 *
 */
static void
csio_hw_configure(struct csio_hw *hw)
{
	int reset = 1;
	int rv;
	u32 param[1];

	rv = csio_hw_dev_ready(hw);
	if (rv != 0) {
		CSIO_INC_STATS(hw, n_err_fatal);
		csio_post_event(&hw->sm, CSIO_HWE_FATAL);
		goto out;
	}

	/* HW version */
	hw->chip_ver = (char)csio_rd_reg32(hw, PL_REV);

	/* Needed for FW download */
	rv = csio_hw_get_flash_params(hw);
	if (rv != 0) {
		csio_err(hw, "Failed to get serial flash params rv:%d\n", rv);
		csio_post_event(&hw->sm, CSIO_HWE_FATAL);
		goto out;
	}

	/* Set pci completion timeout value to 4 seconds. */
	csio_set_pcie_completion_timeout(hw, 0xd);

	csio_hw_set_mem_win(hw);

	rv = csio_hw_get_fw_version(hw, &hw->fwrev);
	if (rv != 0)
		goto out;

	csio_hw_print_fw_version(hw, "Firmware revision");

	rv = csio_do_hello(hw, &hw->fw_state);
	if (rv != 0) {
		CSIO_INC_STATS(hw, n_err_fatal);
		csio_post_event(&hw->sm, CSIO_HWE_FATAL);
		goto out;
	}

	/* Read vpd */
	rv = csio_hw_get_vpd_params(hw, &hw->vpd);
	if (rv != 0)
		goto out;

	if (csio_is_hw_master(hw) && hw->fw_state != CSIO_DEV_STATE_INIT) {
		rv = csio_hw_check_fw_version(hw);
		if (rv == -EINVAL) {

			/* Do firmware update */
			spin_unlock_irq(&hw->lock);
			rv = csio_hw_flash_fw(hw);
			spin_lock_irq(&hw->lock);

			if (rv == 0) {
				reset = 0;
				/*
				 * Note that the chip was reset as part of the
				 * firmware upgrade so we don't reset it again
				 * below and grab the new firmware version.
				 */
				rv = csio_hw_check_fw_version(hw);
			}
		}
		/*
		 * If the firmware doesn't support Configuration
		 * Files, use the old Driver-based, hard-wired
		 * initialization.  Otherwise, try using the
		 * Configuration File support and fall back to the
		 * Driver-based initialization if there's no
		 * Configuration File found.
		 */
		if (csio_hw_check_fwconfig(hw, param) == 0) {
			rv = csio_hw_use_fwconfig(hw, reset, param);
			if (rv == -ENOENT)
				goto out;
			if (rv != 0) {
				csio_info(hw,
				    "No Configuration File present "
				    "on adapter.  Using hard-wired "
				    "configuration parameters.\n");
				rv = csio_hw_no_fwconfig(hw, reset);
			}
		} else {
			rv = csio_hw_no_fwconfig(hw, reset);
		}

		if (rv != 0)
			goto out;

	} else {
		if (hw->fw_state == CSIO_DEV_STATE_INIT) {

			/* device parameters */
			rv = csio_get_device_params(hw);
			if (rv != 0)
				goto out;

			/* Get device capabilities */
			rv = csio_config_device_caps(hw);
			if (rv != 0)
				goto out;

			/* Configure SGE */
			csio_wr_sge_init(hw);

			/* Post event to notify completion of configuration */
			csio_post_event(&hw->sm, CSIO_HWE_INIT);
			goto out;
		}
	} /* if not master */

out:
	return;
}

/*
 * csio_hw_initialize - Initialize HW
 * @hw - HW module
 *
 */
static void
csio_hw_initialize(struct csio_hw *hw)
{
	struct csio_mb	*mbp;
	enum fw_retval retval;
	int rv;
	int i;

	if (csio_is_hw_master(hw) && hw->fw_state != CSIO_DEV_STATE_INIT) {
		mbp = mempool_alloc(hw->mb_mempool, GFP_ATOMIC);
		if (!mbp)
			goto out;

		csio_mb_initialize(hw, mbp, CSIO_MB_DEFAULT_TMO, NULL);

		if (csio_mb_issue(hw, mbp)) {
			csio_err(hw, "Issue of FW_INITIALIZE_CMD failed!\n");
			goto free_and_out;
		}

		retval = csio_mb_fw_retval(mbp);
		if (retval != FW_SUCCESS) {
			csio_err(hw, "FW_INITIALIZE_CMD returned 0x%x!\n",
				 retval);
			goto free_and_out;
		}

		mempool_free(mbp, hw->mb_mempool);
	}

	rv = csio_get_fcoe_resinfo(hw);
	if (rv != 0) {
		csio_err(hw, "Failed to read fcoe resource info: %d\n", rv);
		goto out;
	}

	spin_unlock_irq(&hw->lock);
	rv = csio_config_queues(hw);
	spin_lock_irq(&hw->lock);

	if (rv != 0) {
		csio_err(hw, "Config of queues failed!: %d\n", rv);
		goto out;
	}

	for (i = 0; i < hw->num_pports; i++)
		hw->pport[i].mod_type = FW_PORT_MOD_TYPE_NA;

	if (csio_is_hw_master(hw) && hw->fw_state != CSIO_DEV_STATE_INIT) {
		rv = csio_enable_ports(hw);
		if (rv != 0) {
			csio_err(hw, "Failed to enable ports: %d\n", rv);
			goto out;
		}
	}

	csio_post_event(&hw->sm, CSIO_HWE_INIT_DONE);
	return;

free_and_out:
	mempool_free(mbp, hw->mb_mempool);
out:
	return;
}

#define PF_INTR_MASK (PFSW | PFCIM)

/*
 * csio_hw_intr_enable - Enable HW interrupts
 * @hw: Pointer to HW module.
 *
 * Enable interrupts in HW registers.
 */
static void
csio_hw_intr_enable(struct csio_hw *hw)
{
	uint16_t vec = (uint16_t)csio_get_mb_intr_idx(csio_hw_to_mbm(hw));
	uint32_t pf = SOURCEPF_GET(csio_rd_reg32(hw, PL_WHOAMI));
	uint32_t pl = csio_rd_reg32(hw, PL_INT_ENABLE);

	/*
	 * Set aivec for MSI/MSIX. PCIE_PF_CFG.INTXType is set up
	 * by FW, so do nothing for INTX.
	 */
	if (hw->intr_mode == CSIO_IM_MSIX)
		csio_set_reg_field(hw, MYPF_REG(PCIE_PF_CFG),
				   AIVEC(AIVEC_MASK), vec);
	else if (hw->intr_mode == CSIO_IM_MSI)
		csio_set_reg_field(hw, MYPF_REG(PCIE_PF_CFG),
				   AIVEC(AIVEC_MASK), 0);

	csio_wr_reg32(hw, PF_INTR_MASK, MYPF_REG(PL_PF_INT_ENABLE));

	/* Turn on MB interrupts - this will internally flush PIO as well */
	csio_mb_intr_enable(hw);

	/* These are common registers - only a master can modify them */
	if (csio_is_hw_master(hw)) {
		/*
		 * Disable the Serial FLASH interrupt, if enabled!
		 */
		pl &= (~SF);
		csio_wr_reg32(hw, pl, PL_INT_ENABLE);

		csio_wr_reg32(hw, ERR_CPL_EXCEED_IQE_SIZE |
			      EGRESS_SIZE_ERR | ERR_INVALID_CIDX_INC |
			      ERR_CPL_OPCODE_0 | ERR_DROPPED_DB |
			      ERR_DATA_CPL_ON_HIGH_QID1 |
			      ERR_DATA_CPL_ON_HIGH_QID0 | ERR_BAD_DB_PIDX3 |
			      ERR_BAD_DB_PIDX2 | ERR_BAD_DB_PIDX1 |
			      ERR_BAD_DB_PIDX0 | ERR_ING_CTXT_PRIO |
			      ERR_EGR_CTXT_PRIO | INGRESS_SIZE_ERR,
			      SGE_INT_ENABLE3);
		csio_set_reg_field(hw, PL_INT_MAP0, 0, 1 << pf);
	}

	hw->flags |= CSIO_HWF_HW_INTR_ENABLED;

}

/*
 * csio_hw_intr_disable - Disable HW interrupts
 * @hw: Pointer to HW module.
 *
 * Turn off Mailbox and PCI_PF_CFG interrupts.
 */
void
csio_hw_intr_disable(struct csio_hw *hw)
{
	uint32_t pf = SOURCEPF_GET(csio_rd_reg32(hw, PL_WHOAMI));

	if (!(hw->flags & CSIO_HWF_HW_INTR_ENABLED))
		return;

	hw->flags &= ~CSIO_HWF_HW_INTR_ENABLED;

	csio_wr_reg32(hw, 0, MYPF_REG(PL_PF_INT_ENABLE));
	if (csio_is_hw_master(hw))
		csio_set_reg_field(hw, PL_INT_MAP0, 1 << pf, 0);

	/* Turn off MB interrupts */
	csio_mb_intr_disable(hw);

}

static void
csio_hw_fatal_err(struct csio_hw *hw)
{
	csio_set_reg_field(hw, SGE_CONTROL, GLOBALENABLE, 0);
	csio_hw_intr_disable(hw);

	/* Do not reset HW, we may need FW state for debugging */
	csio_fatal(hw, "HW Fatal error encountered!\n");
}

/*****************************************************************************/
/* START: HW SM                                                              */
/*****************************************************************************/
/*
 * csio_hws_uninit - Uninit state
 * @hw - HW module
 * @evt - Event
 *
 */
static void
csio_hws_uninit(struct csio_hw *hw, enum csio_hw_ev evt)
{
	hw->prev_evt = hw->cur_evt;
	hw->cur_evt = evt;
	CSIO_INC_STATS(hw, n_evt_sm[evt]);

	switch (evt) {
	case CSIO_HWE_CFG:
		csio_set_state(&hw->sm, csio_hws_configuring);
		csio_hw_configure(hw);
		break;

	default:
		CSIO_INC_STATS(hw, n_evt_unexp);
		break;
	}
}

/*
 * csio_hws_configuring - Configuring state
 * @hw - HW module
 * @evt - Event
 *
 */
static void
csio_hws_configuring(struct csio_hw *hw, enum csio_hw_ev evt)
{
	hw->prev_evt = hw->cur_evt;
	hw->cur_evt = evt;
	CSIO_INC_STATS(hw, n_evt_sm[evt]);

	switch (evt) {
	case CSIO_HWE_INIT:
		csio_set_state(&hw->sm, csio_hws_initializing);
		csio_hw_initialize(hw);
		break;

	case CSIO_HWE_INIT_DONE:
		csio_set_state(&hw->sm, csio_hws_ready);
		/* Fan out event to all lnode SMs */
		csio_notify_lnodes(hw, CSIO_LN_NOTIFY_HWREADY);
		break;

	case CSIO_HWE_FATAL:
		csio_set_state(&hw->sm, csio_hws_uninit);
		break;

	case CSIO_HWE_PCI_REMOVE:
		csio_do_bye(hw);
		break;
	default:
		CSIO_INC_STATS(hw, n_evt_unexp);
		break;
	}
}

/*
 * csio_hws_initializing - Initialiazing state
 * @hw - HW module
 * @evt - Event
 *
 */
static void
csio_hws_initializing(struct csio_hw *hw, enum csio_hw_ev evt)
{
	hw->prev_evt = hw->cur_evt;
	hw->cur_evt = evt;
	CSIO_INC_STATS(hw, n_evt_sm[evt]);

	switch (evt) {
	case CSIO_HWE_INIT_DONE:
		csio_set_state(&hw->sm, csio_hws_ready);

		/* Fan out event to all lnode SMs */
		csio_notify_lnodes(hw, CSIO_LN_NOTIFY_HWREADY);

		/* Enable interrupts */
		csio_hw_intr_enable(hw);
		break;

	case CSIO_HWE_FATAL:
		csio_set_state(&hw->sm, csio_hws_uninit);
		break;

	case CSIO_HWE_PCI_REMOVE:
		csio_do_bye(hw);
		break;

	default:
		CSIO_INC_STATS(hw, n_evt_unexp);
		break;
	}
}

/*
 * csio_hws_ready - Ready state
 * @hw - HW module
 * @evt - Event
 *
 */
static void
csio_hws_ready(struct csio_hw *hw, enum csio_hw_ev evt)
{
	/* Remember the event */
	hw->evtflag = evt;

	hw->prev_evt = hw->cur_evt;
	hw->cur_evt = evt;
	CSIO_INC_STATS(hw, n_evt_sm[evt]);

	switch (evt) {
	case CSIO_HWE_HBA_RESET:
	case CSIO_HWE_FW_DLOAD:
	case CSIO_HWE_SUSPEND:
	case CSIO_HWE_PCI_REMOVE:
	case CSIO_HWE_PCIERR_DETECTED:
		csio_set_state(&hw->sm, csio_hws_quiescing);
		/* cleanup all outstanding cmds */
		if (evt == CSIO_HWE_HBA_RESET ||
		    evt == CSIO_HWE_PCIERR_DETECTED)
			csio_scsim_cleanup_io(csio_hw_to_scsim(hw), false);
		else
			csio_scsim_cleanup_io(csio_hw_to_scsim(hw), true);

		csio_hw_intr_disable(hw);
		csio_hw_mbm_cleanup(hw);
		csio_evtq_stop(hw);
		csio_notify_lnodes(hw, CSIO_LN_NOTIFY_HWSTOP);
		csio_evtq_flush(hw);
		csio_mgmtm_cleanup(csio_hw_to_mgmtm(hw));
		csio_post_event(&hw->sm, CSIO_HWE_QUIESCED);
		break;

	case CSIO_HWE_FATAL:
		csio_set_state(&hw->sm, csio_hws_uninit);
		break;

	default:
		CSIO_INC_STATS(hw, n_evt_unexp);
		break;
	}
}

/*
 * csio_hws_quiescing - Quiescing state
 * @hw - HW module
 * @evt - Event
 *
 */
static void
csio_hws_quiescing(struct csio_hw *hw, enum csio_hw_ev evt)
{
	hw->prev_evt = hw->cur_evt;
	hw->cur_evt = evt;
	CSIO_INC_STATS(hw, n_evt_sm[evt]);

	switch (evt) {
	case CSIO_HWE_QUIESCED:
		switch (hw->evtflag) {
		case CSIO_HWE_FW_DLOAD:
			csio_set_state(&hw->sm, csio_hws_resetting);
			/* Download firmware */
			/* Fall through */

		case CSIO_HWE_HBA_RESET:
			csio_set_state(&hw->sm, csio_hws_resetting);
			/* Start reset of the HBA */
			csio_notify_lnodes(hw, CSIO_LN_NOTIFY_HWRESET);
			csio_wr_destroy_queues(hw, false);
			csio_do_reset(hw, false);
			csio_post_event(&hw->sm, CSIO_HWE_HBA_RESET_DONE);
			break;

		case CSIO_HWE_PCI_REMOVE:
			csio_set_state(&hw->sm, csio_hws_removing);
			csio_notify_lnodes(hw, CSIO_LN_NOTIFY_HWREMOVE);
			csio_wr_destroy_queues(hw, true);
			/* Now send the bye command */
			csio_do_bye(hw);
			break;

		case CSIO_HWE_SUSPEND:
			csio_set_state(&hw->sm, csio_hws_quiesced);
			break;

		case CSIO_HWE_PCIERR_DETECTED:
			csio_set_state(&hw->sm, csio_hws_pcierr);
			csio_wr_destroy_queues(hw, false);
			break;

		default:
			CSIO_INC_STATS(hw, n_evt_unexp);
			break;

		}
		break;

	default:
		CSIO_INC_STATS(hw, n_evt_unexp);
		break;
	}
}

/*
 * csio_hws_quiesced - Quiesced state
 * @hw - HW module
 * @evt - Event
 *
 */
static void
csio_hws_quiesced(struct csio_hw *hw, enum csio_hw_ev evt)
{
	hw->prev_evt = hw->cur_evt;
	hw->cur_evt = evt;
	CSIO_INC_STATS(hw, n_evt_sm[evt]);

	switch (evt) {
	case CSIO_HWE_RESUME:
		csio_set_state(&hw->sm, csio_hws_configuring);
		csio_hw_configure(hw);
		break;

	default:
		CSIO_INC_STATS(hw, n_evt_unexp);
		break;
	}
}

/*
 * csio_hws_resetting - HW Resetting state
 * @hw - HW module
 * @evt - Event
 *
 */
static void
csio_hws_resetting(struct csio_hw *hw, enum csio_hw_ev evt)
{
	hw->prev_evt = hw->cur_evt;
	hw->cur_evt = evt;
	CSIO_INC_STATS(hw, n_evt_sm[evt]);

	switch (evt) {
	case CSIO_HWE_HBA_RESET_DONE:
		csio_evtq_start(hw);
		csio_set_state(&hw->sm, csio_hws_configuring);
		csio_hw_configure(hw);
		break;

	default:
		CSIO_INC_STATS(hw, n_evt_unexp);
		break;
	}
}

/*
 * csio_hws_removing - PCI Hotplug removing state
 * @hw - HW module
 * @evt - Event
 *
 */
static void
csio_hws_removing(struct csio_hw *hw, enum csio_hw_ev evt)
{
	hw->prev_evt = hw->cur_evt;
	hw->cur_evt = evt;
	CSIO_INC_STATS(hw, n_evt_sm[evt]);

	switch (evt) {
	case CSIO_HWE_HBA_RESET:
		if (!csio_is_hw_master(hw))
			break;
		/*
		 * The BYE should have alerady been issued, so we cant
		 * use the mailbox interface. Hence we use the PL_RST
		 * register directly.
		 */
		csio_err(hw, "Resetting HW and waiting 2 seconds...\n");
		csio_wr_reg32(hw, PIORSTMODE | PIORST, PL_RST);
		mdelay(2000);
		break;

	/* Should never receive any new events */
	default:
		CSIO_INC_STATS(hw, n_evt_unexp);
		break;

	}
}

/*
 * csio_hws_pcierr - PCI Error state
 * @hw - HW module
 * @evt - Event
 *
 */
static void
csio_hws_pcierr(struct csio_hw *hw, enum csio_hw_ev evt)
{
	hw->prev_evt = hw->cur_evt;
	hw->cur_evt = evt;
	CSIO_INC_STATS(hw, n_evt_sm[evt]);

	switch (evt) {
	case CSIO_HWE_PCIERR_SLOT_RESET:
		csio_evtq_start(hw);
		csio_set_state(&hw->sm, csio_hws_configuring);
		csio_hw_configure(hw);
		break;

	default:
		CSIO_INC_STATS(hw, n_evt_unexp);
		break;
	}
}

/*****************************************************************************/
/* END: HW SM                                                                */
/*****************************************************************************/

/* Slow path handlers */
struct intr_info {
	unsigned int mask;       /* bits to check in interrupt status */
	const char *msg;         /* message to print or NULL */
	short stat_idx;          /* stat counter to increment or -1 */
	unsigned short fatal;    /* whether the condition reported is fatal */
};

/*
 *	csio_handle_intr_status - table driven interrupt handler
 *	@hw: HW instance
 *	@reg: the interrupt status register to process
 *	@acts: table of interrupt actions
 *
 *	A table driven interrupt handler that applies a set of masks to an
 *	interrupt status word and performs the corresponding actions if the
 *	interrupts described by the mask have occured.  The actions include
 *	optionally emitting a warning or alert message. The table is terminated
 *	by an entry specifying mask 0.  Returns the number of fatal interrupt
 *	conditions.
 */
static int
csio_handle_intr_status(struct csio_hw *hw, unsigned int reg,
				 const struct intr_info *acts)
{
	int fatal = 0;
	unsigned int mask = 0;
	unsigned int status = csio_rd_reg32(hw, reg);

	for ( ; acts->mask; ++acts) {
		if (!(status & acts->mask))
			continue;
		if (acts->fatal) {
			fatal++;
			csio_fatal(hw, "Fatal %s (0x%x)\n",
				    acts->msg, status & acts->mask);
		} else if (acts->msg)
			csio_info(hw, "%s (0x%x)\n",
				    acts->msg, status & acts->mask);
		mask |= acts->mask;
	}
	status &= mask;
	if (status)                           /* clear processed interrupts */
		csio_wr_reg32(hw, status, reg);
	return fatal;
}

/*
 * Interrupt handler for the PCIE module.
 */
static void
csio_pcie_intr_handler(struct csio_hw *hw)
{
	static struct intr_info sysbus_intr_info[] = {
		{ RNPP, "RXNP array parity error", -1, 1 },
		{ RPCP, "RXPC array parity error", -1, 1 },
		{ RCIP, "RXCIF array parity error", -1, 1 },
		{ RCCP, "Rx completions control array parity error", -1, 1 },
		{ RFTP, "RXFT array parity error", -1, 1 },
		{ 0, NULL, 0, 0 }
	};
	static struct intr_info pcie_port_intr_info[] = {
		{ TPCP, "TXPC array parity error", -1, 1 },
		{ TNPP, "TXNP array parity error", -1, 1 },
		{ TFTP, "TXFT array parity error", -1, 1 },
		{ TCAP, "TXCA array parity error", -1, 1 },
		{ TCIP, "TXCIF array parity error", -1, 1 },
		{ RCAP, "RXCA array parity error", -1, 1 },
		{ OTDD, "outbound request TLP discarded", -1, 1 },
		{ RDPE, "Rx data parity error", -1, 1 },
		{ TDUE, "Tx uncorrectable data error", -1, 1 },
		{ 0, NULL, 0, 0 }
	};
	static struct intr_info pcie_intr_info[] = {
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
		{ UNXSPLCPLERR, "PCI unexpected split completion error", -1,
		  0 },
		{ 0, NULL, 0, 0 }
	};

	int fat;

	fat = csio_handle_intr_status(hw,
				    PCIE_CORE_UTL_SYSTEM_BUS_AGENT_STATUS,
				    sysbus_intr_info) +
	      csio_handle_intr_status(hw,
				    PCIE_CORE_UTL_PCI_EXPRESS_PORT_STATUS,
				    pcie_port_intr_info) +
	      csio_handle_intr_status(hw, PCIE_INT_CAUSE, pcie_intr_info);
	if (fat)
		csio_hw_fatal_err(hw);
}

/*
 * TP interrupt handler.
 */
static void csio_tp_intr_handler(struct csio_hw *hw)
{
	static struct intr_info tp_intr_info[] = {
		{ 0x3fffffff, "TP parity error", -1, 1 },
		{ FLMTXFLSTEMPTY, "TP out of Tx pages", -1, 1 },
		{ 0, NULL, 0, 0 }
	};

	if (csio_handle_intr_status(hw, TP_INT_CAUSE, tp_intr_info))
		csio_hw_fatal_err(hw);
}

/*
 * SGE interrupt handler.
 */
static void csio_sge_intr_handler(struct csio_hw *hw)
{
	uint64_t v;

	static struct intr_info sge_intr_info[] = {
		{ ERR_CPL_EXCEED_IQE_SIZE,
		  "SGE received CPL exceeding IQE size", -1, 1 },
		{ ERR_INVALID_CIDX_INC,
		  "SGE GTS CIDX increment too large", -1, 0 },
		{ ERR_CPL_OPCODE_0, "SGE received 0-length CPL", -1, 0 },
		{ ERR_DROPPED_DB, "SGE doorbell dropped", -1, 0 },
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
		{ 0, NULL, 0, 0 }
	};

	v = (uint64_t)csio_rd_reg32(hw, SGE_INT_CAUSE1) |
	    ((uint64_t)csio_rd_reg32(hw, SGE_INT_CAUSE2) << 32);
	if (v) {
		csio_fatal(hw, "SGE parity error (%#llx)\n",
			    (unsigned long long)v);
		csio_wr_reg32(hw, (uint32_t)(v & 0xFFFFFFFF),
						SGE_INT_CAUSE1);
		csio_wr_reg32(hw, (uint32_t)(v >> 32), SGE_INT_CAUSE2);
	}

	v |= csio_handle_intr_status(hw, SGE_INT_CAUSE3, sge_intr_info);

	if (csio_handle_intr_status(hw, SGE_INT_CAUSE3, sge_intr_info) ||
	    v != 0)
		csio_hw_fatal_err(hw);
}

#define CIM_OBQ_INTR (OBQULP0PARERR | OBQULP1PARERR | OBQULP2PARERR |\
		      OBQULP3PARERR | OBQSGEPARERR | OBQNCSIPARERR)
#define CIM_IBQ_INTR (IBQTP0PARERR | IBQTP1PARERR | IBQULPPARERR |\
		      IBQSGEHIPARERR | IBQSGELOPARERR | IBQNCSIPARERR)

/*
 * CIM interrupt handler.
 */
static void csio_cim_intr_handler(struct csio_hw *hw)
{
	static struct intr_info cim_intr_info[] = {
		{ PREFDROPINT, "CIM control register prefetch drop", -1, 1 },
		{ CIM_OBQ_INTR, "CIM OBQ parity error", -1, 1 },
		{ CIM_IBQ_INTR, "CIM IBQ parity error", -1, 1 },
		{ MBUPPARERR, "CIM mailbox uP parity error", -1, 1 },
		{ MBHOSTPARERR, "CIM mailbox host parity error", -1, 1 },
		{ TIEQINPARERRINT, "CIM TIEQ outgoing parity error", -1, 1 },
		{ TIEQOUTPARERRINT, "CIM TIEQ incoming parity error", -1, 1 },
		{ 0, NULL, 0, 0 }
	};
	static struct intr_info cim_upintr_info[] = {
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
		{ 0, NULL, 0, 0 }
	};

	int fat;

	fat = csio_handle_intr_status(hw, CIM_HOST_INT_CAUSE,
				    cim_intr_info) +
	      csio_handle_intr_status(hw, CIM_HOST_UPACC_INT_CAUSE,
				    cim_upintr_info);
	if (fat)
		csio_hw_fatal_err(hw);
}

/*
 * ULP RX interrupt handler.
 */
static void csio_ulprx_intr_handler(struct csio_hw *hw)
{
	static struct intr_info ulprx_intr_info[] = {
		{ 0x1800000, "ULPRX context error", -1, 1 },
		{ 0x7fffff, "ULPRX parity error", -1, 1 },
		{ 0, NULL, 0, 0 }
	};

	if (csio_handle_intr_status(hw, ULP_RX_INT_CAUSE, ulprx_intr_info))
		csio_hw_fatal_err(hw);
}

/*
 * ULP TX interrupt handler.
 */
static void csio_ulptx_intr_handler(struct csio_hw *hw)
{
	static struct intr_info ulptx_intr_info[] = {
		{ PBL_BOUND_ERR_CH3, "ULPTX channel 3 PBL out of bounds", -1,
		  0 },
		{ PBL_BOUND_ERR_CH2, "ULPTX channel 2 PBL out of bounds", -1,
		  0 },
		{ PBL_BOUND_ERR_CH1, "ULPTX channel 1 PBL out of bounds", -1,
		  0 },
		{ PBL_BOUND_ERR_CH0, "ULPTX channel 0 PBL out of bounds", -1,
		  0 },
		{ 0xfffffff, "ULPTX parity error", -1, 1 },
		{ 0, NULL, 0, 0 }
	};

	if (csio_handle_intr_status(hw, ULP_TX_INT_CAUSE, ulptx_intr_info))
		csio_hw_fatal_err(hw);
}

/*
 * PM TX interrupt handler.
 */
static void csio_pmtx_intr_handler(struct csio_hw *hw)
{
	static struct intr_info pmtx_intr_info[] = {
		{ PCMD_LEN_OVFL0, "PMTX channel 0 pcmd too large", -1, 1 },
		{ PCMD_LEN_OVFL1, "PMTX channel 1 pcmd too large", -1, 1 },
		{ PCMD_LEN_OVFL2, "PMTX channel 2 pcmd too large", -1, 1 },
		{ ZERO_C_CMD_ERROR, "PMTX 0-length pcmd", -1, 1 },
		{ 0xffffff0, "PMTX framing error", -1, 1 },
		{ OESPI_PAR_ERROR, "PMTX oespi parity error", -1, 1 },
		{ DB_OPTIONS_PAR_ERROR, "PMTX db_options parity error", -1,
		  1 },
		{ ICSPI_PAR_ERROR, "PMTX icspi parity error", -1, 1 },
		{ C_PCMD_PAR_ERROR, "PMTX c_pcmd parity error", -1, 1},
		{ 0, NULL, 0, 0 }
	};

	if (csio_handle_intr_status(hw, PM_TX_INT_CAUSE, pmtx_intr_info))
		csio_hw_fatal_err(hw);
}

/*
 * PM RX interrupt handler.
 */
static void csio_pmrx_intr_handler(struct csio_hw *hw)
{
	static struct intr_info pmrx_intr_info[] = {
		{ ZERO_E_CMD_ERROR, "PMRX 0-length pcmd", -1, 1 },
		{ 0x3ffff0, "PMRX framing error", -1, 1 },
		{ OCSPI_PAR_ERROR, "PMRX ocspi parity error", -1, 1 },
		{ DB_OPTIONS_PAR_ERROR, "PMRX db_options parity error", -1,
		  1 },
		{ IESPI_PAR_ERROR, "PMRX iespi parity error", -1, 1 },
		{ E_PCMD_PAR_ERROR, "PMRX e_pcmd parity error", -1, 1},
		{ 0, NULL, 0, 0 }
	};

	if (csio_handle_intr_status(hw, PM_RX_INT_CAUSE, pmrx_intr_info))
		csio_hw_fatal_err(hw);
}

/*
 * CPL switch interrupt handler.
 */
static void csio_cplsw_intr_handler(struct csio_hw *hw)
{
	static struct intr_info cplsw_intr_info[] = {
		{ CIM_OP_MAP_PERR, "CPLSW CIM op_map parity error", -1, 1 },
		{ CIM_OVFL_ERROR, "CPLSW CIM overflow", -1, 1 },
		{ TP_FRAMING_ERROR, "CPLSW TP framing error", -1, 1 },
		{ SGE_FRAMING_ERROR, "CPLSW SGE framing error", -1, 1 },
		{ CIM_FRAMING_ERROR, "CPLSW CIM framing error", -1, 1 },
		{ ZERO_SWITCH_ERROR, "CPLSW no-switch error", -1, 1 },
		{ 0, NULL, 0, 0 }
	};

	if (csio_handle_intr_status(hw, CPL_INTR_CAUSE, cplsw_intr_info))
		csio_hw_fatal_err(hw);
}

/*
 * LE interrupt handler.
 */
static void csio_le_intr_handler(struct csio_hw *hw)
{
	static struct intr_info le_intr_info[] = {
		{ LIPMISS, "LE LIP miss", -1, 0 },
		{ LIP0, "LE 0 LIP error", -1, 0 },
		{ PARITYERR, "LE parity error", -1, 1 },
		{ UNKNOWNCMD, "LE unknown command", -1, 1 },
		{ REQQPARERR, "LE request queue parity error", -1, 1 },
		{ 0, NULL, 0, 0 }
	};

	if (csio_handle_intr_status(hw, LE_DB_INT_CAUSE, le_intr_info))
		csio_hw_fatal_err(hw);
}

/*
 * MPS interrupt handler.
 */
static void csio_mps_intr_handler(struct csio_hw *hw)
{
	static struct intr_info mps_rx_intr_info[] = {
		{ 0xffffff, "MPS Rx parity error", -1, 1 },
		{ 0, NULL, 0, 0 }
	};
	static struct intr_info mps_tx_intr_info[] = {
		{ TPFIFO, "MPS Tx TP FIFO parity error", -1, 1 },
		{ NCSIFIFO, "MPS Tx NC-SI FIFO parity error", -1, 1 },
		{ TXDATAFIFO, "MPS Tx data FIFO parity error", -1, 1 },
		{ TXDESCFIFO, "MPS Tx desc FIFO parity error", -1, 1 },
		{ BUBBLE, "MPS Tx underflow", -1, 1 },
		{ SECNTERR, "MPS Tx SOP/EOP error", -1, 1 },
		{ FRMERR, "MPS Tx framing error", -1, 1 },
		{ 0, NULL, 0, 0 }
	};
	static struct intr_info mps_trc_intr_info[] = {
		{ FILTMEM, "MPS TRC filter parity error", -1, 1 },
		{ PKTFIFO, "MPS TRC packet FIFO parity error", -1, 1 },
		{ MISCPERR, "MPS TRC misc parity error", -1, 1 },
		{ 0, NULL, 0, 0 }
	};
	static struct intr_info mps_stat_sram_intr_info[] = {
		{ 0x1fffff, "MPS statistics SRAM parity error", -1, 1 },
		{ 0, NULL, 0, 0 }
	};
	static struct intr_info mps_stat_tx_intr_info[] = {
		{ 0xfffff, "MPS statistics Tx FIFO parity error", -1, 1 },
		{ 0, NULL, 0, 0 }
	};
	static struct intr_info mps_stat_rx_intr_info[] = {
		{ 0xffffff, "MPS statistics Rx FIFO parity error", -1, 1 },
		{ 0, NULL, 0, 0 }
	};
	static struct intr_info mps_cls_intr_info[] = {
		{ MATCHSRAM, "MPS match SRAM parity error", -1, 1 },
		{ MATCHTCAM, "MPS match TCAM parity error", -1, 1 },
		{ HASHSRAM, "MPS hash SRAM parity error", -1, 1 },
		{ 0, NULL, 0, 0 }
	};

	int fat;

	fat = csio_handle_intr_status(hw, MPS_RX_PERR_INT_CAUSE,
				    mps_rx_intr_info) +
	      csio_handle_intr_status(hw, MPS_TX_INT_CAUSE,
				    mps_tx_intr_info) +
	      csio_handle_intr_status(hw, MPS_TRC_INT_CAUSE,
				    mps_trc_intr_info) +
	      csio_handle_intr_status(hw, MPS_STAT_PERR_INT_CAUSE_SRAM,
				    mps_stat_sram_intr_info) +
	      csio_handle_intr_status(hw, MPS_STAT_PERR_INT_CAUSE_TX_FIFO,
				    mps_stat_tx_intr_info) +
	      csio_handle_intr_status(hw, MPS_STAT_PERR_INT_CAUSE_RX_FIFO,
				    mps_stat_rx_intr_info) +
	      csio_handle_intr_status(hw, MPS_CLS_INT_CAUSE,
				    mps_cls_intr_info);

	csio_wr_reg32(hw, 0, MPS_INT_CAUSE);
	csio_rd_reg32(hw, MPS_INT_CAUSE);                    /* flush */
	if (fat)
		csio_hw_fatal_err(hw);
}

#define MEM_INT_MASK (PERR_INT_CAUSE | ECC_CE_INT_CAUSE | ECC_UE_INT_CAUSE)

/*
 * EDC/MC interrupt handler.
 */
static void csio_mem_intr_handler(struct csio_hw *hw, int idx)
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

	v = csio_rd_reg32(hw, addr) & MEM_INT_MASK;
	if (v & PERR_INT_CAUSE)
		csio_fatal(hw, "%s FIFO parity error\n", name[idx]);
	if (v & ECC_CE_INT_CAUSE) {
		uint32_t cnt = ECC_CECNT_GET(csio_rd_reg32(hw, cnt_addr));

		csio_wr_reg32(hw, ECC_CECNT_MASK, cnt_addr);
		csio_warn(hw, "%u %s correctable ECC data error%s\n",
			    cnt, name[idx], cnt > 1 ? "s" : "");
	}
	if (v & ECC_UE_INT_CAUSE)
		csio_fatal(hw, "%s uncorrectable ECC data error\n", name[idx]);

	csio_wr_reg32(hw, v, addr);
	if (v & (PERR_INT_CAUSE | ECC_UE_INT_CAUSE))
		csio_hw_fatal_err(hw);
}

/*
 * MA interrupt handler.
 */
static void csio_ma_intr_handler(struct csio_hw *hw)
{
	uint32_t v, status = csio_rd_reg32(hw, MA_INT_CAUSE);

	if (status & MEM_PERR_INT_CAUSE)
		csio_fatal(hw, "MA parity error, parity status %#x\n",
			    csio_rd_reg32(hw, MA_PARITY_ERROR_STATUS));
	if (status & MEM_WRAP_INT_CAUSE) {
		v = csio_rd_reg32(hw, MA_INT_WRAP_STATUS);
		csio_fatal(hw,
		   "MA address wrap-around error by client %u to address %#x\n",
		   MEM_WRAP_CLIENT_NUM_GET(v), MEM_WRAP_ADDRESS_GET(v) << 4);
	}
	csio_wr_reg32(hw, status, MA_INT_CAUSE);
	csio_hw_fatal_err(hw);
}

/*
 * SMB interrupt handler.
 */
static void csio_smb_intr_handler(struct csio_hw *hw)
{
	static struct intr_info smb_intr_info[] = {
		{ MSTTXFIFOPARINT, "SMB master Tx FIFO parity error", -1, 1 },
		{ MSTRXFIFOPARINT, "SMB master Rx FIFO parity error", -1, 1 },
		{ SLVFIFOPARINT, "SMB slave FIFO parity error", -1, 1 },
		{ 0, NULL, 0, 0 }
	};

	if (csio_handle_intr_status(hw, SMB_INT_CAUSE, smb_intr_info))
		csio_hw_fatal_err(hw);
}

/*
 * NC-SI interrupt handler.
 */
static void csio_ncsi_intr_handler(struct csio_hw *hw)
{
	static struct intr_info ncsi_intr_info[] = {
		{ CIM_DM_PRTY_ERR, "NC-SI CIM parity error", -1, 1 },
		{ MPS_DM_PRTY_ERR, "NC-SI MPS parity error", -1, 1 },
		{ TXFIFO_PRTY_ERR, "NC-SI Tx FIFO parity error", -1, 1 },
		{ RXFIFO_PRTY_ERR, "NC-SI Rx FIFO parity error", -1, 1 },
		{ 0, NULL, 0, 0 }
	};

	if (csio_handle_intr_status(hw, NCSI_INT_CAUSE, ncsi_intr_info))
		csio_hw_fatal_err(hw);
}

/*
 * XGMAC interrupt handler.
 */
static void csio_xgmac_intr_handler(struct csio_hw *hw, int port)
{
	uint32_t v = csio_rd_reg32(hw, PORT_REG(port, XGMAC_PORT_INT_CAUSE));

	v &= TXFIFO_PRTY_ERR | RXFIFO_PRTY_ERR;
	if (!v)
		return;

	if (v & TXFIFO_PRTY_ERR)
		csio_fatal(hw, "XGMAC %d Tx FIFO parity error\n", port);
	if (v & RXFIFO_PRTY_ERR)
		csio_fatal(hw, "XGMAC %d Rx FIFO parity error\n", port);
	csio_wr_reg32(hw, v, PORT_REG(port, XGMAC_PORT_INT_CAUSE));
	csio_hw_fatal_err(hw);
}

/*
 * PL interrupt handler.
 */
static void csio_pl_intr_handler(struct csio_hw *hw)
{
	static struct intr_info pl_intr_info[] = {
		{ FATALPERR, "T4 fatal parity error", -1, 1 },
		{ PERRVFID, "PL VFID_MAP parity error", -1, 1 },
		{ 0, NULL, 0, 0 }
	};

	if (csio_handle_intr_status(hw, PL_PL_INT_CAUSE, pl_intr_info))
		csio_hw_fatal_err(hw);
}

/*
 *	csio_hw_slow_intr_handler - control path interrupt handler
 *	@hw: HW module
 *
 *	Interrupt handler for non-data global interrupt events, e.g., errors.
 *	The designation 'slow' is because it involves register reads, while
 *	data interrupts typically don't involve any MMIOs.
 */
int
csio_hw_slow_intr_handler(struct csio_hw *hw)
{
	uint32_t cause = csio_rd_reg32(hw, PL_INT_CAUSE);

	if (!(cause & CSIO_GLBL_INTR_MASK)) {
		CSIO_INC_STATS(hw, n_plint_unexp);
		return 0;
	}

	csio_dbg(hw, "Slow interrupt! cause: 0x%x\n", cause);

	CSIO_INC_STATS(hw, n_plint_cnt);

	if (cause & CIM)
		csio_cim_intr_handler(hw);

	if (cause & MPS)
		csio_mps_intr_handler(hw);

	if (cause & NCSI)
		csio_ncsi_intr_handler(hw);

	if (cause & PL)
		csio_pl_intr_handler(hw);

	if (cause & SMB)
		csio_smb_intr_handler(hw);

	if (cause & XGMAC0)
		csio_xgmac_intr_handler(hw, 0);

	if (cause & XGMAC1)
		csio_xgmac_intr_handler(hw, 1);

	if (cause & XGMAC_KR0)
		csio_xgmac_intr_handler(hw, 2);

	if (cause & XGMAC_KR1)
		csio_xgmac_intr_handler(hw, 3);

	if (cause & PCIE)
		csio_pcie_intr_handler(hw);

	if (cause & MC)
		csio_mem_intr_handler(hw, MEM_MC);

	if (cause & EDC0)
		csio_mem_intr_handler(hw, MEM_EDC0);

	if (cause & EDC1)
		csio_mem_intr_handler(hw, MEM_EDC1);

	if (cause & LE)
		csio_le_intr_handler(hw);

	if (cause & TP)
		csio_tp_intr_handler(hw);

	if (cause & MA)
		csio_ma_intr_handler(hw);

	if (cause & PM_TX)
		csio_pmtx_intr_handler(hw);

	if (cause & PM_RX)
		csio_pmrx_intr_handler(hw);

	if (cause & ULP_RX)
		csio_ulprx_intr_handler(hw);

	if (cause & CPL_SWITCH)
		csio_cplsw_intr_handler(hw);

	if (cause & SGE)
		csio_sge_intr_handler(hw);

	if (cause & ULP_TX)
		csio_ulptx_intr_handler(hw);

	/* Clear the interrupts just processed for which we are the master. */
	csio_wr_reg32(hw, cause & CSIO_GLBL_INTR_MASK, PL_INT_CAUSE);
	csio_rd_reg32(hw, PL_INT_CAUSE); /* flush */

	return 1;
}

/*****************************************************************************
 * HW <--> mailbox interfacing routines.
 ****************************************************************************/
/*
 * csio_mberr_worker - Worker thread (dpc) for mailbox/error completions
 *
 * @data: Private data pointer.
 *
 * Called from worker thread context.
 */
static void
csio_mberr_worker(void *data)
{
	struct csio_hw *hw = (struct csio_hw *)data;
	struct csio_mbm *mbm = &hw->mbm;
	LIST_HEAD(cbfn_q);
	struct csio_mb *mbp_next;
	int rv;

	del_timer_sync(&mbm->timer);

	spin_lock_irq(&hw->lock);
	if (list_empty(&mbm->cbfn_q)) {
		spin_unlock_irq(&hw->lock);
		return;
	}

	list_splice_tail_init(&mbm->cbfn_q, &cbfn_q);
	mbm->stats.n_cbfnq = 0;

	/* Try to start waiting mailboxes */
	if (!list_empty(&mbm->req_q)) {
		mbp_next = list_first_entry(&mbm->req_q, struct csio_mb, list);
		list_del_init(&mbp_next->list);

		rv = csio_mb_issue(hw, mbp_next);
		if (rv != 0)
			list_add_tail(&mbp_next->list, &mbm->req_q);
		else
			CSIO_DEC_STATS(mbm, n_activeq);
	}
	spin_unlock_irq(&hw->lock);

	/* Now callback completions */
	csio_mb_completions(hw, &cbfn_q);
}

/*
 * csio_hw_mb_timer - Top-level Mailbox timeout handler.
 *
 * @data: private data pointer
 *
 **/
static void
csio_hw_mb_timer(uintptr_t data)
{
	struct csio_hw *hw = (struct csio_hw *)data;
	struct csio_mb *mbp = NULL;

	spin_lock_irq(&hw->lock);
	mbp = csio_mb_tmo_handler(hw);
	spin_unlock_irq(&hw->lock);

	/* Call back the function for the timed-out Mailbox */
	if (mbp)
		mbp->mb_cbfn(hw, mbp);

}

/*
 * csio_hw_mbm_cleanup - Cleanup Mailbox module.
 * @hw: HW module
 *
 * Called with lock held, should exit with lock held.
 * Cancels outstanding mailboxes (waiting, in-flight) and gathers them
 * into a local queue. Drops lock and calls the completions. Holds
 * lock and returns.
 */
static void
csio_hw_mbm_cleanup(struct csio_hw *hw)
{
	LIST_HEAD(cbfn_q);

	csio_mb_cancel_all(hw, &cbfn_q);

	spin_unlock_irq(&hw->lock);
	csio_mb_completions(hw, &cbfn_q);
	spin_lock_irq(&hw->lock);
}

/*****************************************************************************
 * Event handling
 ****************************************************************************/
int
csio_enqueue_evt(struct csio_hw *hw, enum csio_evt type, void *evt_msg,
			uint16_t len)
{
	struct csio_evt_msg *evt_entry = NULL;

	if (type >= CSIO_EVT_MAX)
		return -EINVAL;

	if (len > CSIO_EVT_MSG_SIZE)
		return -EINVAL;

	if (hw->flags & CSIO_HWF_FWEVT_STOP)
		return -EINVAL;

	if (list_empty(&hw->evt_free_q)) {
		csio_err(hw, "Failed to alloc evt entry, msg type %d len %d\n",
			 type, len);
		return -ENOMEM;
	}

	evt_entry = list_first_entry(&hw->evt_free_q,
				     struct csio_evt_msg, list);
	list_del_init(&evt_entry->list);

	/* copy event msg and queue the event */
	evt_entry->type = type;
	memcpy((void *)evt_entry->data, evt_msg, len);
	list_add_tail(&evt_entry->list, &hw->evt_active_q);

	CSIO_DEC_STATS(hw, n_evt_freeq);
	CSIO_INC_STATS(hw, n_evt_activeq);

	return 0;
}

static int
csio_enqueue_evt_lock(struct csio_hw *hw, enum csio_evt type, void *evt_msg,
			uint16_t len, bool msg_sg)
{
	struct csio_evt_msg *evt_entry = NULL;
	struct csio_fl_dma_buf *fl_sg;
	uint32_t off = 0;
	unsigned long flags;
	int n, ret = 0;

	if (type >= CSIO_EVT_MAX)
		return -EINVAL;

	if (len > CSIO_EVT_MSG_SIZE)
		return -EINVAL;

	spin_lock_irqsave(&hw->lock, flags);
	if (hw->flags & CSIO_HWF_FWEVT_STOP) {
		ret = -EINVAL;
		goto out;
	}

	if (list_empty(&hw->evt_free_q)) {
		csio_err(hw, "Failed to alloc evt entry, msg type %d len %d\n",
			 type, len);
		ret = -ENOMEM;
		goto out;
	}

	evt_entry = list_first_entry(&hw->evt_free_q,
				     struct csio_evt_msg, list);
	list_del_init(&evt_entry->list);

	/* copy event msg and queue the event */
	evt_entry->type = type;

	/* If Payload in SG list*/
	if (msg_sg) {
		fl_sg = (struct csio_fl_dma_buf *) evt_msg;
		for (n = 0; (n < CSIO_MAX_FLBUF_PER_IQWR && off < len); n++) {
			memcpy((void *)((uintptr_t)evt_entry->data + off),
				fl_sg->flbufs[n].vaddr,
				fl_sg->flbufs[n].len);
			off += fl_sg->flbufs[n].len;
		}
	} else
		memcpy((void *)evt_entry->data, evt_msg, len);

	list_add_tail(&evt_entry->list, &hw->evt_active_q);
	CSIO_DEC_STATS(hw, n_evt_freeq);
	CSIO_INC_STATS(hw, n_evt_activeq);
out:
	spin_unlock_irqrestore(&hw->lock, flags);
	return ret;
}

static void
csio_free_evt(struct csio_hw *hw, struct csio_evt_msg *evt_entry)
{
	if (evt_entry) {
		spin_lock_irq(&hw->lock);
		list_del_init(&evt_entry->list);
		list_add_tail(&evt_entry->list, &hw->evt_free_q);
		CSIO_DEC_STATS(hw, n_evt_activeq);
		CSIO_INC_STATS(hw, n_evt_freeq);
		spin_unlock_irq(&hw->lock);
	}
}

void
csio_evtq_flush(struct csio_hw *hw)
{
	uint32_t count;
	count = 30;
	while (hw->flags & CSIO_HWF_FWEVT_PENDING && count--) {
		spin_unlock_irq(&hw->lock);
		msleep(2000);
		spin_lock_irq(&hw->lock);
	}

	CSIO_DB_ASSERT(!(hw->flags & CSIO_HWF_FWEVT_PENDING));
}

static void
csio_evtq_stop(struct csio_hw *hw)
{
	hw->flags |= CSIO_HWF_FWEVT_STOP;
}

static void
csio_evtq_start(struct csio_hw *hw)
{
	hw->flags &= ~CSIO_HWF_FWEVT_STOP;
}

static void
csio_evtq_cleanup(struct csio_hw *hw)
{
	struct list_head *evt_entry, *next_entry;

	/* Release outstanding events from activeq to freeq*/
	if (!list_empty(&hw->evt_active_q))
		list_splice_tail_init(&hw->evt_active_q, &hw->evt_free_q);

	hw->stats.n_evt_activeq = 0;
	hw->flags &= ~CSIO_HWF_FWEVT_PENDING;

	/* Freeup event entry */
	list_for_each_safe(evt_entry, next_entry, &hw->evt_free_q) {
		kfree(evt_entry);
		CSIO_DEC_STATS(hw, n_evt_freeq);
	}

	hw->stats.n_evt_freeq = 0;
}


static void
csio_process_fwevtq_entry(struct csio_hw *hw, void *wr, uint32_t len,
			  struct csio_fl_dma_buf *flb, void *priv)
{
	__u8 op;
	__be64 *data;
	void *msg = NULL;
	uint32_t msg_len = 0;
	bool msg_sg = 0;

	op = ((struct rss_header *) wr)->opcode;
	if (op == CPL_FW6_PLD) {
		CSIO_INC_STATS(hw, n_cpl_fw6_pld);
		if (!flb || !flb->totlen) {
			CSIO_INC_STATS(hw, n_cpl_unexp);
			return;
		}

		msg = (void *) flb;
		msg_len = flb->totlen;
		msg_sg = 1;

		data = (__be64 *) msg;
	} else if (op == CPL_FW6_MSG || op == CPL_FW4_MSG) {

		CSIO_INC_STATS(hw, n_cpl_fw6_msg);
		/* skip RSS header */
		msg = (void *)((uintptr_t)wr + sizeof(__be64));
		msg_len = (op == CPL_FW6_MSG) ? sizeof(struct cpl_fw6_msg) :
			   sizeof(struct cpl_fw4_msg);

		data = (__be64 *) msg;
	} else {
		csio_warn(hw, "unexpected CPL %#x on FW event queue\n", op);
		CSIO_INC_STATS(hw, n_cpl_unexp);
		return;
	}

	/*
	 * Enqueue event to EventQ. Events processing happens
	 * in Event worker thread context
	 */
	if (csio_enqueue_evt_lock(hw, CSIO_EVT_FW, msg,
				  (uint16_t)msg_len, msg_sg))
		CSIO_INC_STATS(hw, n_evt_drop);
}

void
csio_evtq_worker(struct work_struct *work)
{
	struct csio_hw *hw = container_of(work, struct csio_hw, evtq_work);
	struct list_head *evt_entry, *next_entry;
	LIST_HEAD(evt_q);
	struct csio_evt_msg	*evt_msg;
	struct cpl_fw6_msg *msg;
	struct csio_rnode *rn;
	int rv = 0;
	uint8_t evtq_stop = 0;

	csio_dbg(hw, "event worker thread active evts#%d\n",
		 hw->stats.n_evt_activeq);

	spin_lock_irq(&hw->lock);
	while (!list_empty(&hw->evt_active_q)) {
		list_splice_tail_init(&hw->evt_active_q, &evt_q);
		spin_unlock_irq(&hw->lock);

		list_for_each_safe(evt_entry, next_entry, &evt_q) {
			evt_msg = (struct csio_evt_msg *) evt_entry;

			/* Drop events if queue is STOPPED */
			spin_lock_irq(&hw->lock);
			if (hw->flags & CSIO_HWF_FWEVT_STOP)
				evtq_stop = 1;
			spin_unlock_irq(&hw->lock);
			if (evtq_stop) {
				CSIO_INC_STATS(hw, n_evt_drop);
				goto free_evt;
			}

			switch (evt_msg->type) {
			case CSIO_EVT_FW:
				msg = (struct cpl_fw6_msg *)(evt_msg->data);

				if ((msg->opcode == CPL_FW6_MSG ||
				     msg->opcode == CPL_FW4_MSG) &&
				    !msg->type) {
					rv = csio_mb_fwevt_handler(hw,
								msg->data);
					if (!rv)
						break;
					/* Handle any remaining fw events */
					csio_fcoe_fwevt_handler(hw,
							msg->opcode, msg->data);
				} else if (msg->opcode == CPL_FW6_PLD) {

					csio_fcoe_fwevt_handler(hw,
							msg->opcode, msg->data);
				} else {
					csio_warn(hw,
					     "Unhandled FW msg op %x type %x\n",
						  msg->opcode, msg->type);
					CSIO_INC_STATS(hw, n_evt_drop);
				}
				break;

			case CSIO_EVT_MBX:
				csio_mberr_worker(hw);
				break;

			case CSIO_EVT_DEV_LOSS:
				memcpy(&rn, evt_msg->data, sizeof(rn));
				csio_rnode_devloss_handler(rn);
				break;

			default:
				csio_warn(hw, "Unhandled event %x on evtq\n",
					  evt_msg->type);
				CSIO_INC_STATS(hw, n_evt_unexp);
				break;
			}
free_evt:
			csio_free_evt(hw, evt_msg);
		}

		spin_lock_irq(&hw->lock);
	}
	hw->flags &= ~CSIO_HWF_FWEVT_PENDING;
	spin_unlock_irq(&hw->lock);
}

int
csio_fwevtq_handler(struct csio_hw *hw)
{
	int rv;

	if (csio_q_iqid(hw, hw->fwevt_iq_idx) == CSIO_MAX_QID) {
		CSIO_INC_STATS(hw, n_int_stray);
		return -EINVAL;
	}

	rv = csio_wr_process_iq_idx(hw, hw->fwevt_iq_idx,
			   csio_process_fwevtq_entry, NULL);
	return rv;
}

/****************************************************************************
 * Entry points
 ****************************************************************************/

/* Management module */
/*
 * csio_mgmt_req_lookup - Lookup the given IO req exist in Active Q.
 * mgmt - mgmt module
 * @io_req - io request
 *
 * Return - 0:if given IO Req exists in active Q.
 *          -EINVAL  :if lookup fails.
 */
int
csio_mgmt_req_lookup(struct csio_mgmtm *mgmtm, struct csio_ioreq *io_req)
{
	struct list_head *tmp;

	/* Lookup ioreq in the ACTIVEQ */
	list_for_each(tmp, &mgmtm->active_q) {
		if (io_req == (struct csio_ioreq *)tmp)
			return 0;
	}
	return -EINVAL;
}

#define	ECM_MIN_TMO	1000	/* Minimum timeout value for req */

/*
 * csio_mgmts_tmo_handler - MGMT IO Timeout handler.
 * @data - Event data.
 *
 * Return - none.
 */
static void
csio_mgmt_tmo_handler(uintptr_t data)
{
	struct csio_mgmtm *mgmtm = (struct csio_mgmtm *) data;
	struct list_head *tmp;
	struct csio_ioreq *io_req;

	csio_dbg(mgmtm->hw, "Mgmt timer invoked!\n");

	spin_lock_irq(&mgmtm->hw->lock);

	list_for_each(tmp, &mgmtm->active_q) {
		io_req = (struct csio_ioreq *) tmp;
		io_req->tmo -= min_t(uint32_t, io_req->tmo, ECM_MIN_TMO);

		if (!io_req->tmo) {
			/* Dequeue the request from retry Q. */
			tmp = csio_list_prev(tmp);
			list_del_init(&io_req->sm.sm_list);
			if (io_req->io_cbfn) {
				/* io_req will be freed by completion handler */
				io_req->wr_status = -ETIMEDOUT;
				io_req->io_cbfn(mgmtm->hw, io_req);
			} else {
				CSIO_DB_ASSERT(0);
			}
		}
	}

	/* If retry queue is not empty, re-arm timer */
	if (!list_empty(&mgmtm->active_q))
		mod_timer(&mgmtm->mgmt_timer,
			  jiffies + msecs_to_jiffies(ECM_MIN_TMO));
	spin_unlock_irq(&mgmtm->hw->lock);
}

static void
csio_mgmtm_cleanup(struct csio_mgmtm *mgmtm)
{
	struct csio_hw *hw = mgmtm->hw;
	struct csio_ioreq *io_req;
	struct list_head *tmp;
	uint32_t count;

	count = 30;
	/* Wait for all outstanding req to complete gracefully */
	while ((!list_empty(&mgmtm->active_q)) && count--) {
		spin_unlock_irq(&hw->lock);
		msleep(2000);
		spin_lock_irq(&hw->lock);
	}

	/* release outstanding req from ACTIVEQ */
	list_for_each(tmp, &mgmtm->active_q) {
		io_req = (struct csio_ioreq *) tmp;
		tmp = csio_list_prev(tmp);
		list_del_init(&io_req->sm.sm_list);
		mgmtm->stats.n_active--;
		if (io_req->io_cbfn) {
			/* io_req will be freed by completion handler */
			io_req->wr_status = -ETIMEDOUT;
			io_req->io_cbfn(mgmtm->hw, io_req);
		}
	}
}

/*
 * csio_mgmt_init - Mgmt module init entry point
 * @mgmtsm - mgmt module
 * @hw	 - HW module
 *
 * Initialize mgmt timer, resource wait queue, active queue,
 * completion q. Allocate Egress and Ingress
 * WR queues and save off the queue index returned by the WR
 * module for future use. Allocate and save off mgmt reqs in the
 * mgmt_req_freelist for future use. Make sure their SM is initialized
 * to uninit state.
 * Returns: 0 - on success
 *          -ENOMEM   - on error.
 */
static int
csio_mgmtm_init(struct csio_mgmtm *mgmtm, struct csio_hw *hw)
{
	struct timer_list *timer = &mgmtm->mgmt_timer;

	init_timer(timer);
	timer->function = csio_mgmt_tmo_handler;
	timer->data = (unsigned long)mgmtm;

	INIT_LIST_HEAD(&mgmtm->active_q);
	INIT_LIST_HEAD(&mgmtm->cbfn_q);

	mgmtm->hw = hw;
	/*mgmtm->iq_idx = hw->fwevt_iq_idx;*/

	return 0;
}

/*
 * csio_mgmtm_exit - MGMT module exit entry point
 * @mgmtsm - mgmt module
 *
 * This function called during MGMT module uninit.
 * Stop timers, free ioreqs allocated.
 * Returns: None
 *
 */
static void
csio_mgmtm_exit(struct csio_mgmtm *mgmtm)
{
	del_timer_sync(&mgmtm->mgmt_timer);
}


/**
 * csio_hw_start - Kicks off the HW State machine
 * @hw:		Pointer to HW module.
 *
 * It is assumed that the initialization is a synchronous operation.
 * So when we return afer posting the event, the HW SM should be in
 * the ready state, if there were no errors during init.
 */
int
csio_hw_start(struct csio_hw *hw)
{
	spin_lock_irq(&hw->lock);
	csio_post_event(&hw->sm, CSIO_HWE_CFG);
	spin_unlock_irq(&hw->lock);

	if (csio_is_hw_ready(hw))
		return 0;
	else
		return -EINVAL;
}

int
csio_hw_stop(struct csio_hw *hw)
{
	csio_post_event(&hw->sm, CSIO_HWE_PCI_REMOVE);

	if (csio_is_hw_removing(hw))
		return 0;
	else
		return -EINVAL;
}

/* Max reset retries */
#define CSIO_MAX_RESET_RETRIES	3

/**
 * csio_hw_reset - Reset the hardware
 * @hw:		HW module.
 *
 * Caller should hold lock across this function.
 */
int
csio_hw_reset(struct csio_hw *hw)
{
	if (!csio_is_hw_master(hw))
		return -EPERM;

	if (hw->rst_retries >= CSIO_MAX_RESET_RETRIES) {
		csio_dbg(hw, "Max hw reset attempts reached..");
		return -EINVAL;
	}

	hw->rst_retries++;
	csio_post_event(&hw->sm, CSIO_HWE_HBA_RESET);

	if (csio_is_hw_ready(hw)) {
		hw->rst_retries = 0;
		hw->stats.n_reset_start = jiffies_to_msecs(jiffies);
		return 0;
	} else
		return -EINVAL;
}

/*
 * csio_hw_get_device_id - Caches the Adapter's vendor & device id.
 * @hw: HW module.
 */
static void
csio_hw_get_device_id(struct csio_hw *hw)
{
	/* Is the adapter device id cached already ?*/
	if (csio_is_dev_id_cached(hw))
		return;

	/* Get the PCI vendor & device id */
	pci_read_config_word(hw->pdev, PCI_VENDOR_ID,
			     &hw->params.pci.vendor_id);
	pci_read_config_word(hw->pdev, PCI_DEVICE_ID,
			     &hw->params.pci.device_id);

	csio_dev_id_cached(hw);

} /* csio_hw_get_device_id */

/*
 * csio_hw_set_description - Set the model, description of the hw.
 * @hw: HW module.
 * @ven_id: PCI Vendor ID
 * @dev_id: PCI Device ID
 */
static void
csio_hw_set_description(struct csio_hw *hw, uint16_t ven_id, uint16_t dev_id)
{
	uint32_t adap_type, prot_type;

	if (ven_id == CSIO_VENDOR_ID) {
		prot_type = (dev_id & CSIO_ASIC_DEVID_PROTO_MASK);
		adap_type = (dev_id & CSIO_ASIC_DEVID_TYPE_MASK);

		if (prot_type == CSIO_FPGA) {
			memcpy(hw->model_desc,
				csio_fcoe_adapters[13].description, 32);
		} else if (prot_type == CSIO_T4_FCOE_ASIC) {
			memcpy(hw->hw_ver,
			       csio_fcoe_adapters[adap_type].model_no, 16);
			memcpy(hw->model_desc,
				csio_fcoe_adapters[adap_type].description, 32);
		} else {
			char tempName[32] = "Chelsio FCoE Controller";
			memcpy(hw->model_desc, tempName, 32);

			CSIO_DB_ASSERT(0);
		}
	}
} /* csio_hw_set_description */

/**
 * csio_hw_init - Initialize HW module.
 * @hw:		Pointer to HW module.
 *
 * Initialize the members of the HW module.
 */
int
csio_hw_init(struct csio_hw *hw)
{
	int rv = -EINVAL;
	uint32_t i;
	uint16_t ven_id, dev_id;
	struct csio_evt_msg	*evt_entry;

	INIT_LIST_HEAD(&hw->sm.sm_list);
	csio_init_state(&hw->sm, csio_hws_uninit);
	spin_lock_init(&hw->lock);
	INIT_LIST_HEAD(&hw->sln_head);

	/* Get the PCI vendor & device id */
	csio_hw_get_device_id(hw);

	strcpy(hw->name, CSIO_HW_NAME);

	/* Set the model & its description */

	ven_id = hw->params.pci.vendor_id;
	dev_id = hw->params.pci.device_id;

	csio_hw_set_description(hw, ven_id, dev_id);

	/* Initialize default log level */
	hw->params.log_level = (uint32_t) csio_dbg_level;

	csio_set_fwevt_intr_idx(hw, -1);
	csio_set_nondata_intr_idx(hw, -1);

	/* Init all the modules: Mailbox, WorkRequest and Transport */
	if (csio_mbm_init(csio_hw_to_mbm(hw), hw, csio_hw_mb_timer))
		goto err;

	rv = csio_wrm_init(csio_hw_to_wrm(hw), hw);
	if (rv)
		goto err_mbm_exit;

	rv = csio_scsim_init(csio_hw_to_scsim(hw), hw);
	if (rv)
		goto err_wrm_exit;

	rv = csio_mgmtm_init(csio_hw_to_mgmtm(hw), hw);
	if (rv)
		goto err_scsim_exit;
	/* Pre-allocate evtq and initialize them */
	INIT_LIST_HEAD(&hw->evt_active_q);
	INIT_LIST_HEAD(&hw->evt_free_q);
	for (i = 0; i < csio_evtq_sz; i++) {

		evt_entry = kzalloc(sizeof(struct csio_evt_msg), GFP_KERNEL);
		if (!evt_entry) {
			csio_err(hw, "Failed to initialize eventq");
			goto err_evtq_cleanup;
		}

		list_add_tail(&evt_entry->list, &hw->evt_free_q);
		CSIO_INC_STATS(hw, n_evt_freeq);
	}

	hw->dev_num = dev_num;
	dev_num++;

	return 0;

err_evtq_cleanup:
	csio_evtq_cleanup(hw);
	csio_mgmtm_exit(csio_hw_to_mgmtm(hw));
err_scsim_exit:
	csio_scsim_exit(csio_hw_to_scsim(hw));
err_wrm_exit:
	csio_wrm_exit(csio_hw_to_wrm(hw), hw);
err_mbm_exit:
	csio_mbm_exit(csio_hw_to_mbm(hw));
err:
	return rv;
}

/**
 * csio_hw_exit - Un-initialize HW module.
 * @hw:		Pointer to HW module.
 *
 */
void
csio_hw_exit(struct csio_hw *hw)
{
	csio_evtq_cleanup(hw);
	csio_mgmtm_exit(csio_hw_to_mgmtm(hw));
	csio_scsim_exit(csio_hw_to_scsim(hw));
	csio_wrm_exit(csio_hw_to_wrm(hw), hw);
	csio_mbm_exit(csio_hw_to_mbm(hw));
}
