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

int csio_dbg_level = 0xFEFF;
unsigned int csio_port_mask = 0xf;

/* Default FW event queue entries. */
static uint32_t csio_evtq_sz = CSIO_EVTQ_SIZE;

/* Default MSI param level */
int csio_msi = 2;

/* FCoE function instances */
static int dev_num;

/* FCoE Adapter types & its description */
static const struct csio_adap_desc csio_t5_fcoe_adapters[] = {
	{"T580-Dbg 10G", "Chelsio T580-Dbg 10G [FCoE]"},
	{"T520-CR 10G", "Chelsio T520-CR 10G [FCoE]"},
	{"T522-CR 10G/1G", "Chelsio T522-CR 10G/1G [FCoE]"},
	{"T540-CR 10G", "Chelsio T540-CR 10G [FCoE]"},
	{"T520-BCH 10G", "Chelsio T520-BCH 10G [FCoE]"},
	{"T540-BCH 10G", "Chelsio T540-BCH 10G [FCoE]"},
	{"T540-CH 10G", "Chelsio T540-CH 10G [FCoE]"},
	{"T520-SO 10G", "Chelsio T520-SO 10G [FCoE]"},
	{"T520-CX4 10G", "Chelsio T520-CX4 10G [FCoE]"},
	{"T520-BT 10G", "Chelsio T520-BT 10G [FCoE]"},
	{"T504-BT 1G", "Chelsio T504-BT 1G [FCoE]"},
	{"B520-SR 10G", "Chelsio B520-SR 10G [FCoE]"},
	{"B504-BT 1G", "Chelsio B504-BT 1G [FCoE]"},
	{"T580-CR 10G", "Chelsio T580-CR 10G [FCoE]"},
	{"T540-LP-CR 10G", "Chelsio T540-LP-CR 10G [FCoE]"},
	{"AMSTERDAM 10G", "Chelsio AMSTERDAM 10G [FCoE]"},
	{"T580-LP-CR 40G", "Chelsio T580-LP-CR 40G [FCoE]"},
	{"T520-LL-CR 10G", "Chelsio T520-LL-CR 10G [FCoE]"},
	{"T560-CR 40G", "Chelsio T560-CR 40G [FCoE]"},
	{"T580-CR 40G", "Chelsio T580-CR 40G [FCoE]"},
	{"T580-SO 40G", "Chelsio T580-SO 40G [FCoE]"},
	{"T502-BT 1G", "Chelsio T502-BT 1G [FCoE]"}
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
int
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

/*
 *	csio_hw_tp_wr_bits_indirect - set/clear bits in an indirect TP register
 *	@hw: the adapter
 *	@addr: the indirect TP register address
 *	@mask: specifies the field within the register to modify
 *	@val: new value for the field
 *
 *	Sets a field of an indirect TP register to the given value.
 */
void
csio_hw_tp_wr_bits_indirect(struct csio_hw *hw, unsigned int addr,
			unsigned int mask, unsigned int val)
{
	csio_wr_reg32(hw, addr, TP_PIO_ADDR_A);
	val |= csio_rd_reg32(hw, TP_PIO_DATA_A) & ~mask;
	csio_wr_reg32(hw, val, TP_PIO_DATA_A);
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

static int
csio_memory_write(struct csio_hw *hw, int mtype, u32 addr, u32 len, u32 *buf)
{
	return hw->chip_ops->chip_memory_rw(hw, MEMWIN_CSIOSTOR, mtype,
					    addr, len, buf, 0);
}

/*
 * EEPROM reads take a few tens of us while writes can take a bit over 5 ms.
 */
#define EEPROM_MAX_RD_POLL	40
#define EEPROM_MAX_WR_POLL	6
#define EEPROM_STAT_ADDR	0x7bfc
#define VPD_BASE		0x400
#define VPD_BASE_OLD		0
#define VPD_LEN			1024
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
	*data = le32_to_cpu(*(__le32 *)data);

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
	if (csio_rd_reg32(hw, SF_OP_A) & SF_BUSY_F)
		return -EBUSY;

	csio_wr_reg32(hw,  SF_LOCK_V(lock) | SF_CONT_V(cont) |
		      BYTECNT_V(byte_cnt - 1), SF_OP_A);
	ret = csio_hw_wait_op_done_val(hw, SF_OP_A, SF_BUSY_F, 0, SF_ATTEMPTS,
				       10, NULL);
	if (!ret)
		*valp = csio_rd_reg32(hw, SF_DATA_A);
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
	if (csio_rd_reg32(hw, SF_OP_A) & SF_BUSY_F)
		return -EBUSY;

	csio_wr_reg32(hw, val, SF_DATA_A);
	csio_wr_reg32(hw, SF_CONT_V(cont) | BYTECNT_V(byte_cnt - 1) |
		      OP_V(1) | SF_LOCK_V(lock), SF_OP_A);

	return csio_hw_wait_op_done_val(hw, SF_OP_A, SF_BUSY_F, 0, SF_ATTEMPTS,
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
			csio_wr_reg32(hw, 0, SF_OP_A);    /* unlock SF */
		if (ret)
			return ret;
		if (byte_oriented)
			*data = (__force __u32) htonl(*data);
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

	csio_wr_reg32(hw, 0, SF_OP_A);    /* unlock SF */

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
	csio_wr_reg32(hw, 0, SF_OP_A);    /* unlock SF */
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
	csio_wr_reg32(hw, 0, SF_OP_A);    /* unlock SF */
	return 0;
}

static void
csio_hw_print_fw_version(struct csio_hw *hw, char *str)
{
	csio_info(hw, "%s: %u.%u.%u.%u\n", str,
		    FW_HDR_FW_VER_MAJOR_G(hw->fwrev),
		    FW_HDR_FW_VER_MINOR_G(hw->fwrev),
		    FW_HDR_FW_VER_MICRO_G(hw->fwrev),
		    FW_HDR_FW_VER_BUILD_G(hw->fwrev));
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
	return csio_hw_read_flash(hw, FLASH_FW_START +
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

	if (size > FLASH_FW_MAX_SIZE) {
		csio_err(hw, "FW image too large, max is %u bytes\n",
			    FLASH_FW_MAX_SIZE);
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
			  FLASH_FW_START_SEC, FLASH_FW_START_SEC + i - 1);

	ret = csio_hw_flash_erase_sectors(hw, FLASH_FW_START_SEC,
					  FLASH_FW_START_SEC + i - 1);
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
	ret = csio_hw_write_flash(hw, FLASH_FW_START, SF_PAGE_SIZE, first_page);
	if (ret)
		goto out;

	csio_dbg(hw, "Writing Flash .. start:%d end:%d\n",
		    FW_IMG_START, FW_IMG_START + size);

	addr = FLASH_FW_START;
	for (size -= SF_PAGE_SIZE; size; size -= SF_PAGE_SIZE) {
		addr += SF_PAGE_SIZE;
		fw_data += SF_PAGE_SIZE;
		ret = csio_hw_write_flash(hw, addr, SF_PAGE_SIZE, fw_data);
		if (ret)
			goto out;
	}

	ret = csio_hw_write_flash(hw,
				  FLASH_FW_START +
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
	csio_wr_reg32(hw, 0, SF_OP_A);    /* unlock SF */
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

/*****************************************************************************/
/* HW State machine assists                                                  */
/*****************************************************************************/

static int
csio_hw_dev_ready(struct csio_hw *hw)
{
	uint32_t reg;
	int cnt = 6;
	int src_pf;

	while (((reg = csio_rd_reg32(hw, PL_WHOAMI_A)) == 0xFFFFFFFF) &&
	       (--cnt != 0))
		mdelay(100);

	if (csio_is_t5(hw->pdev->device & CSIO_HW_CHIP_MASK))
		src_pf = SOURCEPF_G(reg);
	else
		src_pf = T6_SOURCEPF_G(reg);

	if ((cnt == 0) && (((int32_t)(src_pf) < 0) ||
			   (src_pf >= CSIO_MAX_PFN))) {
		csio_err(hw, "PL_WHOAMI returned 0x%x, cnt:%d\n", reg, cnt);
		return -EIO;
	}

	hw->pfn = src_pf;

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

retry:
	csio_mb_hello(hw, mbp, CSIO_MB_DEFAULT_TMO, hw->pfn,
		      hw->pfn, CSIO_MASTER_MAY, NULL);

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

			spin_unlock_irq(&hw->lock);
			msleep(50);
			spin_lock_irq(&hw->lock);
			waiting -= 50;

			/*
			 * If neither Error nor Initialialized are indicated
			 * by the firmware keep waiting till we exaust our
			 * timeout ... and then retry if we haven't exhausted
			 * our retries ...
			 */
			pcie_fw = csio_rd_reg32(hw, PCIE_FW_A);
			if (!(pcie_fw & (PCIE_FW_ERR_F|PCIE_FW_INIT_F))) {
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
				if (pcie_fw & PCIE_FW_ERR_F) {
					*state = CSIO_DEV_STATE_ERR;
					rv = -ETIMEDOUT;
				} else if (pcie_fw & PCIE_FW_INIT_F)
					*state = CSIO_DEV_STATE_INIT;
			}

			/*
			 * If we arrived before a Master PF was selected and
			 * there's not a valid Master PF, grab its identity
			 * for our caller.
			 */
			if (mpfn == PCIE_FW_MASTER_M &&
			    (pcie_fw & PCIE_FW_MASTER_VLD_F))
				mpfn = PCIE_FW_MASTER_G(pcie_fw);
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
		csio_wr_reg32(hw, PIORSTMODE_F | PIORST_F, PL_RST_A);
		mdelay(2000);
		return 0;
	}

	mbp = mempool_alloc(hw->mb_mempool, GFP_ATOMIC);
	if (!mbp) {
		CSIO_INC_STATS(hw, n_err_nomem);
		return -ENOMEM;
	}

	csio_mb_reset(hw, mbp, CSIO_MB_DEFAULT_TMO,
		      PIORSTMODE_F | PIORST_F, 0, NULL);

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
	if (mbox <= PCIE_FW_MASTER_M) {
		struct csio_mb	*mbp;

		mbp = mempool_alloc(hw->mb_mempool, GFP_ATOMIC);
		if (!mbp) {
			CSIO_INC_STATS(hw, n_err_nomem);
			return -ENOMEM;
		}

		csio_mb_reset(hw, mbp, CSIO_MB_DEFAULT_TMO,
			      PIORSTMODE_F | PIORST_F, FW_RESET_CMD_HALT_F,
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
		csio_set_reg_field(hw, CIM_BOOT_CFG_A, UPCRST_F, UPCRST_F);
		csio_set_reg_field(hw, PCIE_FW_A, PCIE_FW_HALT_F,
				   PCIE_FW_HALT_F);
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
		csio_set_reg_field(hw, PCIE_FW_A, PCIE_FW_HALT_F, 0);

		/*
		 * If we've been given a valid mailbox, first try to get the
		 * firmware to do the RESET.  If that works, great and we can
		 * return success.  Otherwise, if we haven't been given a
		 * valid mailbox or the RESET command failed, fall back to
		 * hitting the chip with a hammer.
		 */
		if (mbox <= PCIE_FW_MASTER_M) {
			csio_set_reg_field(hw, CIM_BOOT_CFG_A, UPCRST_F, 0);
			msleep(100);
			if (csio_do_reset(hw, true) == 0)
				return 0;
		}

		csio_wr_reg32(hw, PIORSTMODE_F | PIORST_F, PL_RST_A);
		msleep(2000);
	} else {
		int ms;

		csio_set_reg_field(hw, CIM_BOOT_CFG_A, UPCRST_F, 0);
		for (ms = 0; ms < FW_CMD_MAX_TIMEOUT; ) {
			if (!(csio_rd_reg32(hw, PCIE_FW_A) & PCIE_FW_HALT_F))
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
	rv = csio_hw_validate_caps(hw, mbp);
	if (rv != 0)
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
	retval = FW_CMD_RETVAL_G(ntohl(rsp->retval_len16));
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
	_param[0] = (FW_PARAMS_MNEM_V(FW_PARAMS_MNEM_DEV) |
		     FW_PARAMS_PARAM_X_V(FW_PARAMS_PARAM_DEV_CF));

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
	const char *fw_cfg_file;

	if (csio_is_t5(pci_dev->device & CSIO_HW_CHIP_MASK))
		fw_cfg_file = FW_CFG_NAME_T5;
	else
		fw_cfg_file = FW_CFG_NAME_T6;

	if (request_firmware(&cf, fw_cfg_file, dev) < 0) {
		csio_err(hw, "could not find config file %s, err: %d\n",
			 fw_cfg_file, ret);
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

	mtype = FW_PARAMS_PARAM_Y_G(*fw_cfg_param);
	maddr = FW_PARAMS_PARAM_Z_G(*fw_cfg_param) << 16;

	ret = csio_memory_write(hw, mtype, maddr,
				cf->size + value_to_add, cfg_data);

	if ((ret == 0) && (value_to_add != 0)) {
		union {
			u32 word;
			char buf[4];
		} last;
		size_t size = cf->size & ~0x3;
		int i;

		last.word = cfg_data[size >> 2];
		for (i = value_to_add; i < 4; i++)
			last.buf[i] = 0;
		ret = csio_memory_write(hw, mtype, maddr + size, 4, &last.word);
	}
	if (ret == 0) {
		csio_info(hw, "config file upgraded to %s\n", fw_cfg_file);
		snprintf(path, 64, "%s%s", "/lib/firmware/", fw_cfg_file);
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
	struct csio_mb	*mbp = NULL;
	struct fw_caps_config_cmd *caps_cmd;
	unsigned int mtype, maddr;
	int rv = -EINVAL;
	uint32_t finiver = 0, finicsum = 0, cfcsum = 0;
	char path[64];
	char *config_name = NULL;

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
		/*
		 * config file was not found. Use default
		 * config file from flash.
		 */
		config_name = "On FLASH";
		mtype = FW_MEMTYPE_CF_FLASH;
		maddr = hw->chip_ops->chip_flash_cfg_addr(hw);
	} else {
		config_name = path;
		mtype = FW_PARAMS_PARAM_Y_G(*fw_cfg_param);
		maddr = FW_PARAMS_PARAM_Z_G(*fw_cfg_param) << 16;
	}

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
		htonl(FW_CMD_OP_V(FW_CAPS_CONFIG_CMD) |
		      FW_CMD_REQUEST_F |
		      FW_CMD_READ_F);
	caps_cmd->cfvalid_to_len16 =
		htonl(FW_CAPS_CONFIG_CMD_CFVALID_F |
		      FW_CAPS_CONFIG_CMD_MEMTYPE_CF_V(mtype) |
		      FW_CAPS_CONFIG_CMD_MEMADDR64K_CF_V(maddr >> 16) |
		      FW_LEN16(*caps_cmd));

	if (csio_mb_issue(hw, mbp)) {
		rv = -EINVAL;
		goto bye;
	}

	rv = csio_mb_fw_retval(mbp);
	 /* If the CAPS_CONFIG failed with an ENOENT (for a Firmware
	  * Configuration File in FLASH), our last gasp effort is to use the
	  * Firmware Configuration File which is embedded in the
	  * firmware.  A very few early versions of the firmware didn't
	  * have one embedded but we can ignore those.
	  */
	if (rv == ENOENT) {
		CSIO_INIT_MBP(mbp, caps_cmd, CSIO_MB_DEFAULT_TMO, hw, NULL, 1);
		caps_cmd->op_to_write = htonl(FW_CMD_OP_V(FW_CAPS_CONFIG_CMD) |
					      FW_CMD_REQUEST_F |
					      FW_CMD_READ_F);
		caps_cmd->cfvalid_to_len16 = htonl(FW_LEN16(*caps_cmd));

		if (csio_mb_issue(hw, mbp)) {
			rv = -EINVAL;
			goto bye;
		}

		rv = csio_mb_fw_retval(mbp);
		config_name = "Firmware Default";
	}
	if (rv != FW_SUCCESS)
		goto bye;

	finiver = ntohl(caps_cmd->finiver);
	finicsum = ntohl(caps_cmd->finicsum);
	cfcsum = ntohl(caps_cmd->cfcsum);

	/*
	 * And now tell the firmware to use the configuration we just loaded.
	 */
	caps_cmd->op_to_write =
		htonl(FW_CMD_OP_V(FW_CAPS_CONFIG_CMD) |
		      FW_CMD_REQUEST_F |
		      FW_CMD_WRITE_F);
	caps_cmd->cfvalid_to_len16 = htonl(FW_LEN16(*caps_cmd));

	if (csio_mb_issue(hw, mbp)) {
		rv = -EINVAL;
		goto bye;
	}

	rv = csio_mb_fw_retval(mbp);
	if (rv != FW_SUCCESS) {
		csio_dbg(hw, "FW_CAPS_CONFIG_CMD returned %d!\n", rv);
		goto bye;
	}

	if (finicsum != cfcsum) {
		csio_warn(hw,
		      "Config File checksum mismatch: csum=%#x, computed=%#x\n",
		      finicsum, cfcsum);
	}

	/* Validate device capabilities */
	rv = csio_hw_validate_caps(hw, mbp);
	if (rv != 0)
		goto bye;

	mempool_free(mbp, hw->mb_mempool);
	mbp = NULL;

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

	csio_info(hw, "Successfully configure using Firmware "
		  "Configuration File %s, version %#x, computed checksum %#x\n",
		  config_name, finiver, cfcsum);
	return 0;

	/*
	 * Something bad happened.  Return the error ...
	 */
bye:
	if (mbp)
		mempool_free(mbp, hw->mb_mempool);
	hw->flags &= ~CSIO_HWF_USING_SOFT_PARAMS;
	csio_warn(hw, "Configuration file error %d\n", rv);
	return rv;
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
static int csio_should_install_fs_fw(struct csio_hw *hw, int card_fw_usable,
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
	csio_err(hw, "firmware on card (%u.%u.%u.%u) is %s, "
		"installing firmware %u.%u.%u.%u on card.\n",
		FW_HDR_FW_VER_MAJOR_G(c), FW_HDR_FW_VER_MINOR_G(c),
		FW_HDR_FW_VER_MICRO_G(c), FW_HDR_FW_VER_BUILD_G(c), reason,
		FW_HDR_FW_VER_MAJOR_G(k), FW_HDR_FW_VER_MINOR_G(k),
		FW_HDR_FW_VER_MICRO_G(k), FW_HDR_FW_VER_BUILD_G(k));

	return 1;
}

static struct fw_info fw_info_array[] = {
	{
		.chip = CHELSIO_T5,
		.fs_name = FW_CFG_NAME_T5,
		.fw_mod_name = FW_FNAME_T5,
		.fw_hdr = {
			.chip = FW_HDR_CHIP_T5,
			.fw_ver = __cpu_to_be32(FW_VERSION(T5)),
			.intfver_nic = FW_INTFVER(T5, NIC),
			.intfver_vnic = FW_INTFVER(T5, VNIC),
			.intfver_ri = FW_INTFVER(T5, RI),
			.intfver_iscsi = FW_INTFVER(T5, ISCSI),
			.intfver_fcoe = FW_INTFVER(T5, FCOE),
		},
	}, {
		.chip = CHELSIO_T6,
		.fs_name = FW_CFG_NAME_T6,
		.fw_mod_name = FW_FNAME_T6,
		.fw_hdr = {
			.chip = FW_HDR_CHIP_T6,
			.fw_ver = __cpu_to_be32(FW_VERSION(T6)),
			.intfver_nic = FW_INTFVER(T6, NIC),
			.intfver_vnic = FW_INTFVER(T6, VNIC),
			.intfver_ri = FW_INTFVER(T6, RI),
			.intfver_iscsi = FW_INTFVER(T6, ISCSI),
			.intfver_fcoe = FW_INTFVER(T6, FCOE),
		},
	}
};

static struct fw_info *find_fw_info(int chip)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(fw_info_array); i++) {
		if (fw_info_array[i].chip == chip)
			return &fw_info_array[i];
	}
	return NULL;
}

static int csio_hw_prep_fw(struct csio_hw *hw, struct fw_info *fw_info,
	       const u8 *fw_data, unsigned int fw_size,
	       struct fw_hdr *card_fw, enum csio_dev_state state,
	       int *reset)
{
	int ret, card_fw_usable, fs_fw_usable;
	const struct fw_hdr *fs_fw;
	const struct fw_hdr *drv_fw;

	drv_fw = &fw_info->fw_hdr;

	/* Read the header of the firmware on the card */
	ret = csio_hw_read_flash(hw, FLASH_FW_START,
			    sizeof(*card_fw) / sizeof(uint32_t),
			    (uint32_t *)card_fw, 1);
	if (ret == 0) {
		card_fw_usable = fw_compatible(drv_fw, (const void *)card_fw);
	} else {
		csio_err(hw,
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
	} else if (fs_fw_usable && state == CSIO_DEV_STATE_UNINIT &&
		   csio_should_install_fs_fw(hw, card_fw_usable,
					be32_to_cpu(fs_fw->fw_ver),
					be32_to_cpu(card_fw->fw_ver))) {
		ret = csio_hw_fw_upgrade(hw, hw->pfn, fw_data,
				     fw_size, 0);
		if (ret != 0) {
			csio_err(hw,
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

		csio_err(hw, "Cannot find a usable firmware: "
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
	hw->fwrev = be32_to_cpu(card_fw->fw_ver);
	hw->tp_vers = be32_to_cpu(card_fw->tp_microcode_ver);

bye:
	return ret;
}

/*
 * Returns -EINVAL if attempts to flash the firmware failed
 * else returns 0,
 * if flashing was not attempted because the card had the
 * latest firmware ECANCELED is returned
 */
static int
csio_hw_flash_fw(struct csio_hw *hw, int *reset)
{
	int ret = -ECANCELED;
	const struct firmware *fw;
	struct fw_info *fw_info;
	struct fw_hdr *card_fw;
	struct pci_dev *pci_dev = hw->pdev;
	struct device *dev = &pci_dev->dev ;
	const u8 *fw_data = NULL;
	unsigned int fw_size = 0;
	const char *fw_bin_file;

	/* This is the firmware whose headers the driver was compiled
	 * against
	 */
	fw_info = find_fw_info(CHELSIO_CHIP_VERSION(hw->chip_id));
	if (fw_info == NULL) {
		csio_err(hw,
			"unable to get firmware info for chip %d.\n",
			CHELSIO_CHIP_VERSION(hw->chip_id));
		return -EINVAL;
	}

	if (csio_is_t5(pci_dev->device & CSIO_HW_CHIP_MASK))
		fw_bin_file = FW_FNAME_T5;
	else
		fw_bin_file = FW_FNAME_T6;

	if (request_firmware(&fw, fw_bin_file, dev) < 0) {
		csio_err(hw, "could not find firmware image %s, err: %d\n",
			 fw_bin_file, ret);
	} else {
		fw_data = fw->data;
		fw_size = fw->size;
	}

	/* allocate memory to read the header of the firmware on the
	 * card
	 */
	card_fw = kmalloc(sizeof(*card_fw), GFP_KERNEL);

	/* upgrade FW logic */
	ret = csio_hw_prep_fw(hw, fw_info, fw_data, fw_size, card_fw,
			 hw->fw_state, reset);

	/* Cleaning up */
	if (fw != NULL)
		release_firmware(fw);
	kfree(card_fw);
	return ret;
}

static int csio_hw_check_fwver(struct csio_hw *hw)
{
	if (csio_is_t6(hw->pdev->device & CSIO_HW_CHIP_MASK) &&
	    (hw->fwrev < CSIO_MIN_T6_FW)) {
		csio_hw_print_fw_version(hw, "T6 unsupported fw");
		return -1;
	}

	return 0;
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
	hw->chip_ver = (char)csio_rd_reg32(hw, PL_REV_A);

	/* Needed for FW download */
	rv = csio_hw_get_flash_params(hw);
	if (rv != 0) {
		csio_err(hw, "Failed to get serial flash params rv:%d\n", rv);
		csio_post_event(&hw->sm, CSIO_HWE_FATAL);
		goto out;
	}

	/* Set PCIe completion timeout to 4 seconds */
	if (pci_is_pcie(hw->pdev))
		pcie_capability_clear_and_set_word(hw->pdev, PCI_EXP_DEVCTL2,
				PCI_EXP_DEVCTL2_COMP_TIMEOUT, 0xd);

	hw->chip_ops->chip_set_mem_win(hw, MEMWIN_CSIOSTOR);

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

	csio_hw_get_fw_version(hw, &hw->fwrev);
	csio_hw_get_tp_version(hw, &hw->tp_vers);
	if (csio_is_hw_master(hw) && hw->fw_state != CSIO_DEV_STATE_INIT) {

			/* Do firmware update */
		spin_unlock_irq(&hw->lock);
		rv = csio_hw_flash_fw(hw, &reset);
		spin_lock_irq(&hw->lock);

		if (rv != 0)
			goto out;

		rv = csio_hw_check_fwver(hw);
		if (rv < 0)
			goto out;

		/* If the firmware doesn't support Configuration Files,
		 * return an error.
		 */
		rv = csio_hw_check_fwconfig(hw, param);
		if (rv != 0) {
			csio_info(hw, "Firmware doesn't support "
				  "Firmware Configuration files\n");
			goto out;
		}

		/* The firmware provides us with a memory buffer where we can
		 * load a Configuration File from the host if we want to
		 * override the Configuration File in flash.
		 */
		rv = csio_hw_use_fwconfig(hw, reset, param);
		if (rv == -ENOENT) {
			csio_info(hw, "Could not initialize "
				  "adapter, error%d\n", rv);
			goto out;
		}
		if (rv != 0) {
			csio_info(hw, "Could not initialize "
				  "adapter, error%d\n", rv);
			goto out;
		}

	} else {
		rv = csio_hw_check_fwver(hw);
		if (rv < 0)
			goto out;

		if (hw->fw_state == CSIO_DEV_STATE_INIT) {

			hw->flags |= CSIO_HWF_USING_SOFT_PARAMS;

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

#define PF_INTR_MASK (PFSW_F | PFCIM_F)

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
	u32 pf = 0;
	uint32_t pl = csio_rd_reg32(hw, PL_INT_ENABLE_A);

	if (csio_is_t5(hw->pdev->device & CSIO_HW_CHIP_MASK))
		pf = SOURCEPF_G(csio_rd_reg32(hw, PL_WHOAMI_A));
	else
		pf = T6_SOURCEPF_G(csio_rd_reg32(hw, PL_WHOAMI_A));

	/*
	 * Set aivec for MSI/MSIX. PCIE_PF_CFG.INTXType is set up
	 * by FW, so do nothing for INTX.
	 */
	if (hw->intr_mode == CSIO_IM_MSIX)
		csio_set_reg_field(hw, MYPF_REG(PCIE_PF_CFG_A),
				   AIVEC_V(AIVEC_M), vec);
	else if (hw->intr_mode == CSIO_IM_MSI)
		csio_set_reg_field(hw, MYPF_REG(PCIE_PF_CFG_A),
				   AIVEC_V(AIVEC_M), 0);

	csio_wr_reg32(hw, PF_INTR_MASK, MYPF_REG(PL_PF_INT_ENABLE_A));

	/* Turn on MB interrupts - this will internally flush PIO as well */
	csio_mb_intr_enable(hw);

	/* These are common registers - only a master can modify them */
	if (csio_is_hw_master(hw)) {
		/*
		 * Disable the Serial FLASH interrupt, if enabled!
		 */
		pl &= (~SF_F);
		csio_wr_reg32(hw, pl, PL_INT_ENABLE_A);

		csio_wr_reg32(hw, ERR_CPL_EXCEED_IQE_SIZE_F |
			      EGRESS_SIZE_ERR_F | ERR_INVALID_CIDX_INC_F |
			      ERR_CPL_OPCODE_0_F | ERR_DROPPED_DB_F |
			      ERR_DATA_CPL_ON_HIGH_QID1_F |
			      ERR_DATA_CPL_ON_HIGH_QID0_F | ERR_BAD_DB_PIDX3_F |
			      ERR_BAD_DB_PIDX2_F | ERR_BAD_DB_PIDX1_F |
			      ERR_BAD_DB_PIDX0_F | ERR_ING_CTXT_PRIO_F |
			      ERR_EGR_CTXT_PRIO_F | INGRESS_SIZE_ERR_F,
			      SGE_INT_ENABLE3_A);
		csio_set_reg_field(hw, PL_INT_MAP0_A, 0, 1 << pf);
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
	u32 pf = 0;

	if (csio_is_t5(hw->pdev->device & CSIO_HW_CHIP_MASK))
		pf = SOURCEPF_G(csio_rd_reg32(hw, PL_WHOAMI_A));
	else
		pf = T6_SOURCEPF_G(csio_rd_reg32(hw, PL_WHOAMI_A));

	if (!(hw->flags & CSIO_HWF_HW_INTR_ENABLED))
		return;

	hw->flags &= ~CSIO_HWF_HW_INTR_ENABLED;

	csio_wr_reg32(hw, 0, MYPF_REG(PL_PF_INT_ENABLE_A));
	if (csio_is_hw_master(hw))
		csio_set_reg_field(hw, PL_INT_MAP0_A, 1 << pf, 0);

	/* Turn off MB interrupts */
	csio_mb_intr_disable(hw);

}

void
csio_hw_fatal_err(struct csio_hw *hw)
{
	csio_set_reg_field(hw, SGE_CONTROL_A, GLOBALENABLE_F, 0);
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
		csio_wr_reg32(hw, PIORSTMODE_F | PIORST_F, PL_RST_A);
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
int
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
 * TP interrupt handler.
 */
static void csio_tp_intr_handler(struct csio_hw *hw)
{
	static struct intr_info tp_intr_info[] = {
		{ 0x3fffffff, "TP parity error", -1, 1 },
		{ FLMTXFLSTEMPTY_F, "TP out of Tx pages", -1, 1 },
		{ 0, NULL, 0, 0 }
	};

	if (csio_handle_intr_status(hw, TP_INT_CAUSE_A, tp_intr_info))
		csio_hw_fatal_err(hw);
}

/*
 * SGE interrupt handler.
 */
static void csio_sge_intr_handler(struct csio_hw *hw)
{
	uint64_t v;

	static struct intr_info sge_intr_info[] = {
		{ ERR_CPL_EXCEED_IQE_SIZE_F,
		  "SGE received CPL exceeding IQE size", -1, 1 },
		{ ERR_INVALID_CIDX_INC_F,
		  "SGE GTS CIDX increment too large", -1, 0 },
		{ ERR_CPL_OPCODE_0_F, "SGE received 0-length CPL", -1, 0 },
		{ ERR_DROPPED_DB_F, "SGE doorbell dropped", -1, 0 },
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
		{ ERR_EGR_CTXT_PRIO_F,
		  "SGE too many priority egress contexts", -1, 0 },
		{ INGRESS_SIZE_ERR_F, "SGE illegal ingress QID", -1, 0 },
		{ EGRESS_SIZE_ERR_F, "SGE illegal egress QID", -1, 0 },
		{ 0, NULL, 0, 0 }
	};

	v = (uint64_t)csio_rd_reg32(hw, SGE_INT_CAUSE1_A) |
	    ((uint64_t)csio_rd_reg32(hw, SGE_INT_CAUSE2_A) << 32);
	if (v) {
		csio_fatal(hw, "SGE parity error (%#llx)\n",
			    (unsigned long long)v);
		csio_wr_reg32(hw, (uint32_t)(v & 0xFFFFFFFF),
						SGE_INT_CAUSE1_A);
		csio_wr_reg32(hw, (uint32_t)(v >> 32), SGE_INT_CAUSE2_A);
	}

	v |= csio_handle_intr_status(hw, SGE_INT_CAUSE3_A, sge_intr_info);

	if (csio_handle_intr_status(hw, SGE_INT_CAUSE3_A, sge_intr_info) ||
	    v != 0)
		csio_hw_fatal_err(hw);
}

#define CIM_OBQ_INTR (OBQULP0PARERR_F | OBQULP1PARERR_F | OBQULP2PARERR_F |\
		      OBQULP3PARERR_F | OBQSGEPARERR_F | OBQNCSIPARERR_F)
#define CIM_IBQ_INTR (IBQTP0PARERR_F | IBQTP1PARERR_F | IBQULPPARERR_F |\
		      IBQSGEHIPARERR_F | IBQSGELOPARERR_F | IBQNCSIPARERR_F)

/*
 * CIM interrupt handler.
 */
static void csio_cim_intr_handler(struct csio_hw *hw)
{
	static struct intr_info cim_intr_info[] = {
		{ PREFDROPINT_F, "CIM control register prefetch drop", -1, 1 },
		{ CIM_OBQ_INTR, "CIM OBQ parity error", -1, 1 },
		{ CIM_IBQ_INTR, "CIM IBQ parity error", -1, 1 },
		{ MBUPPARERR_F, "CIM mailbox uP parity error", -1, 1 },
		{ MBHOSTPARERR_F, "CIM mailbox host parity error", -1, 1 },
		{ TIEQINPARERRINT_F, "CIM TIEQ outgoing parity error", -1, 1 },
		{ TIEQOUTPARERRINT_F, "CIM TIEQ incoming parity error", -1, 1 },
		{ 0, NULL, 0, 0 }
	};
	static struct intr_info cim_upintr_info[] = {
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
		{ 0, NULL, 0, 0 }
	};

	int fat;

	fat = csio_handle_intr_status(hw, CIM_HOST_INT_CAUSE_A,
				      cim_intr_info) +
	      csio_handle_intr_status(hw, CIM_HOST_UPACC_INT_CAUSE_A,
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

	if (csio_handle_intr_status(hw, ULP_RX_INT_CAUSE_A, ulprx_intr_info))
		csio_hw_fatal_err(hw);
}

/*
 * ULP TX interrupt handler.
 */
static void csio_ulptx_intr_handler(struct csio_hw *hw)
{
	static struct intr_info ulptx_intr_info[] = {
		{ PBL_BOUND_ERR_CH3_F, "ULPTX channel 3 PBL out of bounds", -1,
		  0 },
		{ PBL_BOUND_ERR_CH2_F, "ULPTX channel 2 PBL out of bounds", -1,
		  0 },
		{ PBL_BOUND_ERR_CH1_F, "ULPTX channel 1 PBL out of bounds", -1,
		  0 },
		{ PBL_BOUND_ERR_CH0_F, "ULPTX channel 0 PBL out of bounds", -1,
		  0 },
		{ 0xfffffff, "ULPTX parity error", -1, 1 },
		{ 0, NULL, 0, 0 }
	};

	if (csio_handle_intr_status(hw, ULP_TX_INT_CAUSE_A, ulptx_intr_info))
		csio_hw_fatal_err(hw);
}

/*
 * PM TX interrupt handler.
 */
static void csio_pmtx_intr_handler(struct csio_hw *hw)
{
	static struct intr_info pmtx_intr_info[] = {
		{ PCMD_LEN_OVFL0_F, "PMTX channel 0 pcmd too large", -1, 1 },
		{ PCMD_LEN_OVFL1_F, "PMTX channel 1 pcmd too large", -1, 1 },
		{ PCMD_LEN_OVFL2_F, "PMTX channel 2 pcmd too large", -1, 1 },
		{ ZERO_C_CMD_ERROR_F, "PMTX 0-length pcmd", -1, 1 },
		{ 0xffffff0, "PMTX framing error", -1, 1 },
		{ OESPI_PAR_ERROR_F, "PMTX oespi parity error", -1, 1 },
		{ DB_OPTIONS_PAR_ERROR_F, "PMTX db_options parity error", -1,
		  1 },
		{ ICSPI_PAR_ERROR_F, "PMTX icspi parity error", -1, 1 },
		{ PMTX_C_PCMD_PAR_ERROR_F, "PMTX c_pcmd parity error", -1, 1},
		{ 0, NULL, 0, 0 }
	};

	if (csio_handle_intr_status(hw, PM_TX_INT_CAUSE_A, pmtx_intr_info))
		csio_hw_fatal_err(hw);
}

/*
 * PM RX interrupt handler.
 */
static void csio_pmrx_intr_handler(struct csio_hw *hw)
{
	static struct intr_info pmrx_intr_info[] = {
		{ ZERO_E_CMD_ERROR_F, "PMRX 0-length pcmd", -1, 1 },
		{ 0x3ffff0, "PMRX framing error", -1, 1 },
		{ OCSPI_PAR_ERROR_F, "PMRX ocspi parity error", -1, 1 },
		{ DB_OPTIONS_PAR_ERROR_F, "PMRX db_options parity error", -1,
		  1 },
		{ IESPI_PAR_ERROR_F, "PMRX iespi parity error", -1, 1 },
		{ PMRX_E_PCMD_PAR_ERROR_F, "PMRX e_pcmd parity error", -1, 1},
		{ 0, NULL, 0, 0 }
	};

	if (csio_handle_intr_status(hw, PM_RX_INT_CAUSE_A, pmrx_intr_info))
		csio_hw_fatal_err(hw);
}

/*
 * CPL switch interrupt handler.
 */
static void csio_cplsw_intr_handler(struct csio_hw *hw)
{
	static struct intr_info cplsw_intr_info[] = {
		{ CIM_OP_MAP_PERR_F, "CPLSW CIM op_map parity error", -1, 1 },
		{ CIM_OVFL_ERROR_F, "CPLSW CIM overflow", -1, 1 },
		{ TP_FRAMING_ERROR_F, "CPLSW TP framing error", -1, 1 },
		{ SGE_FRAMING_ERROR_F, "CPLSW SGE framing error", -1, 1 },
		{ CIM_FRAMING_ERROR_F, "CPLSW CIM framing error", -1, 1 },
		{ ZERO_SWITCH_ERROR_F, "CPLSW no-switch error", -1, 1 },
		{ 0, NULL, 0, 0 }
	};

	if (csio_handle_intr_status(hw, CPL_INTR_CAUSE_A, cplsw_intr_info))
		csio_hw_fatal_err(hw);
}

/*
 * LE interrupt handler.
 */
static void csio_le_intr_handler(struct csio_hw *hw)
{
	enum chip_type chip = CHELSIO_CHIP_VERSION(hw->chip_id);

	static struct intr_info le_intr_info[] = {
		{ LIPMISS_F, "LE LIP miss", -1, 0 },
		{ LIP0_F, "LE 0 LIP error", -1, 0 },
		{ PARITYERR_F, "LE parity error", -1, 1 },
		{ UNKNOWNCMD_F, "LE unknown command", -1, 1 },
		{ REQQPARERR_F, "LE request queue parity error", -1, 1 },
		{ 0, NULL, 0, 0 }
	};

	static struct intr_info t6_le_intr_info[] = {
		{ T6_LIPMISS_F, "LE LIP miss", -1, 0 },
		{ T6_LIP0_F, "LE 0 LIP error", -1, 0 },
		{ TCAMINTPERR_F, "LE parity error", -1, 1 },
		{ T6_UNKNOWNCMD_F, "LE unknown command", -1, 1 },
		{ SSRAMINTPERR_F, "LE request queue parity error", -1, 1 },
		{ 0, NULL, 0, 0 }
	};

	if (csio_handle_intr_status(hw, LE_DB_INT_CAUSE_A,
				    (chip == CHELSIO_T5) ?
				    le_intr_info : t6_le_intr_info))
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
		{ TPFIFO_V(TPFIFO_M), "MPS Tx TP FIFO parity error", -1, 1 },
		{ NCSIFIFO_F, "MPS Tx NC-SI FIFO parity error", -1, 1 },
		{ TXDATAFIFO_V(TXDATAFIFO_M), "MPS Tx data FIFO parity error",
		  -1, 1 },
		{ TXDESCFIFO_V(TXDESCFIFO_M), "MPS Tx desc FIFO parity error",
		  -1, 1 },
		{ BUBBLE_F, "MPS Tx underflow", -1, 1 },
		{ SECNTERR_F, "MPS Tx SOP/EOP error", -1, 1 },
		{ FRMERR_F, "MPS Tx framing error", -1, 1 },
		{ 0, NULL, 0, 0 }
	};
	static struct intr_info mps_trc_intr_info[] = {
		{ FILTMEM_V(FILTMEM_M), "MPS TRC filter parity error", -1, 1 },
		{ PKTFIFO_V(PKTFIFO_M), "MPS TRC packet FIFO parity error",
		  -1, 1 },
		{ MISCPERR_F, "MPS TRC misc parity error", -1, 1 },
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
		{ MATCHSRAM_F, "MPS match SRAM parity error", -1, 1 },
		{ MATCHTCAM_F, "MPS match TCAM parity error", -1, 1 },
		{ HASHSRAM_F, "MPS hash SRAM parity error", -1, 1 },
		{ 0, NULL, 0, 0 }
	};

	int fat;

	fat = csio_handle_intr_status(hw, MPS_RX_PERR_INT_CAUSE_A,
				      mps_rx_intr_info) +
	      csio_handle_intr_status(hw, MPS_TX_INT_CAUSE_A,
				      mps_tx_intr_info) +
	      csio_handle_intr_status(hw, MPS_TRC_INT_CAUSE_A,
				      mps_trc_intr_info) +
	      csio_handle_intr_status(hw, MPS_STAT_PERR_INT_CAUSE_SRAM_A,
				      mps_stat_sram_intr_info) +
	      csio_handle_intr_status(hw, MPS_STAT_PERR_INT_CAUSE_TX_FIFO_A,
				      mps_stat_tx_intr_info) +
	      csio_handle_intr_status(hw, MPS_STAT_PERR_INT_CAUSE_RX_FIFO_A,
				      mps_stat_rx_intr_info) +
	      csio_handle_intr_status(hw, MPS_CLS_INT_CAUSE_A,
				      mps_cls_intr_info);

	csio_wr_reg32(hw, 0, MPS_INT_CAUSE_A);
	csio_rd_reg32(hw, MPS_INT_CAUSE_A);                    /* flush */
	if (fat)
		csio_hw_fatal_err(hw);
}

#define MEM_INT_MASK (PERR_INT_CAUSE_F | ECC_CE_INT_CAUSE_F | \
		      ECC_UE_INT_CAUSE_F)

/*
 * EDC/MC interrupt handler.
 */
static void csio_mem_intr_handler(struct csio_hw *hw, int idx)
{
	static const char name[3][5] = { "EDC0", "EDC1", "MC" };

	unsigned int addr, cnt_addr, v;

	if (idx <= MEM_EDC1) {
		addr = EDC_REG(EDC_INT_CAUSE_A, idx);
		cnt_addr = EDC_REG(EDC_ECC_STATUS_A, idx);
	} else {
		addr = MC_INT_CAUSE_A;
		cnt_addr = MC_ECC_STATUS_A;
	}

	v = csio_rd_reg32(hw, addr) & MEM_INT_MASK;
	if (v & PERR_INT_CAUSE_F)
		csio_fatal(hw, "%s FIFO parity error\n", name[idx]);
	if (v & ECC_CE_INT_CAUSE_F) {
		uint32_t cnt = ECC_CECNT_G(csio_rd_reg32(hw, cnt_addr));

		csio_wr_reg32(hw, ECC_CECNT_V(ECC_CECNT_M), cnt_addr);
		csio_warn(hw, "%u %s correctable ECC data error%s\n",
			    cnt, name[idx], cnt > 1 ? "s" : "");
	}
	if (v & ECC_UE_INT_CAUSE_F)
		csio_fatal(hw, "%s uncorrectable ECC data error\n", name[idx]);

	csio_wr_reg32(hw, v, addr);
	if (v & (PERR_INT_CAUSE_F | ECC_UE_INT_CAUSE_F))
		csio_hw_fatal_err(hw);
}

/*
 * MA interrupt handler.
 */
static void csio_ma_intr_handler(struct csio_hw *hw)
{
	uint32_t v, status = csio_rd_reg32(hw, MA_INT_CAUSE_A);

	if (status & MEM_PERR_INT_CAUSE_F)
		csio_fatal(hw, "MA parity error, parity status %#x\n",
			    csio_rd_reg32(hw, MA_PARITY_ERROR_STATUS_A));
	if (status & MEM_WRAP_INT_CAUSE_F) {
		v = csio_rd_reg32(hw, MA_INT_WRAP_STATUS_A);
		csio_fatal(hw,
		   "MA address wrap-around error by client %u to address %#x\n",
		   MEM_WRAP_CLIENT_NUM_G(v), MEM_WRAP_ADDRESS_G(v) << 4);
	}
	csio_wr_reg32(hw, status, MA_INT_CAUSE_A);
	csio_hw_fatal_err(hw);
}

/*
 * SMB interrupt handler.
 */
static void csio_smb_intr_handler(struct csio_hw *hw)
{
	static struct intr_info smb_intr_info[] = {
		{ MSTTXFIFOPARINT_F, "SMB master Tx FIFO parity error", -1, 1 },
		{ MSTRXFIFOPARINT_F, "SMB master Rx FIFO parity error", -1, 1 },
		{ SLVFIFOPARINT_F, "SMB slave FIFO parity error", -1, 1 },
		{ 0, NULL, 0, 0 }
	};

	if (csio_handle_intr_status(hw, SMB_INT_CAUSE_A, smb_intr_info))
		csio_hw_fatal_err(hw);
}

/*
 * NC-SI interrupt handler.
 */
static void csio_ncsi_intr_handler(struct csio_hw *hw)
{
	static struct intr_info ncsi_intr_info[] = {
		{ CIM_DM_PRTY_ERR_F, "NC-SI CIM parity error", -1, 1 },
		{ MPS_DM_PRTY_ERR_F, "NC-SI MPS parity error", -1, 1 },
		{ TXFIFO_PRTY_ERR_F, "NC-SI Tx FIFO parity error", -1, 1 },
		{ RXFIFO_PRTY_ERR_F, "NC-SI Rx FIFO parity error", -1, 1 },
		{ 0, NULL, 0, 0 }
	};

	if (csio_handle_intr_status(hw, NCSI_INT_CAUSE_A, ncsi_intr_info))
		csio_hw_fatal_err(hw);
}

/*
 * XGMAC interrupt handler.
 */
static void csio_xgmac_intr_handler(struct csio_hw *hw, int port)
{
	uint32_t v = csio_rd_reg32(hw, T5_PORT_REG(port, MAC_PORT_INT_CAUSE_A));

	v &= TXFIFO_PRTY_ERR_F | RXFIFO_PRTY_ERR_F;
	if (!v)
		return;

	if (v & TXFIFO_PRTY_ERR_F)
		csio_fatal(hw, "XGMAC %d Tx FIFO parity error\n", port);
	if (v & RXFIFO_PRTY_ERR_F)
		csio_fatal(hw, "XGMAC %d Rx FIFO parity error\n", port);
	csio_wr_reg32(hw, v, T5_PORT_REG(port, MAC_PORT_INT_CAUSE_A));
	csio_hw_fatal_err(hw);
}

/*
 * PL interrupt handler.
 */
static void csio_pl_intr_handler(struct csio_hw *hw)
{
	static struct intr_info pl_intr_info[] = {
		{ FATALPERR_F, "T4 fatal parity error", -1, 1 },
		{ PERRVFID_F, "PL VFID_MAP parity error", -1, 1 },
		{ 0, NULL, 0, 0 }
	};

	if (csio_handle_intr_status(hw, PL_PL_INT_CAUSE_A, pl_intr_info))
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
	uint32_t cause = csio_rd_reg32(hw, PL_INT_CAUSE_A);

	if (!(cause & CSIO_GLBL_INTR_MASK)) {
		CSIO_INC_STATS(hw, n_plint_unexp);
		return 0;
	}

	csio_dbg(hw, "Slow interrupt! cause: 0x%x\n", cause);

	CSIO_INC_STATS(hw, n_plint_cnt);

	if (cause & CIM_F)
		csio_cim_intr_handler(hw);

	if (cause & MPS_F)
		csio_mps_intr_handler(hw);

	if (cause & NCSI_F)
		csio_ncsi_intr_handler(hw);

	if (cause & PL_F)
		csio_pl_intr_handler(hw);

	if (cause & SMB_F)
		csio_smb_intr_handler(hw);

	if (cause & XGMAC0_F)
		csio_xgmac_intr_handler(hw, 0);

	if (cause & XGMAC1_F)
		csio_xgmac_intr_handler(hw, 1);

	if (cause & XGMAC_KR0_F)
		csio_xgmac_intr_handler(hw, 2);

	if (cause & XGMAC_KR1_F)
		csio_xgmac_intr_handler(hw, 3);

	if (cause & PCIE_F)
		hw->chip_ops->chip_pcie_intr_handler(hw);

	if (cause & MC_F)
		csio_mem_intr_handler(hw, MEM_MC);

	if (cause & EDC0_F)
		csio_mem_intr_handler(hw, MEM_EDC0);

	if (cause & EDC1_F)
		csio_mem_intr_handler(hw, MEM_EDC1);

	if (cause & LE_F)
		csio_le_intr_handler(hw);

	if (cause & TP_F)
		csio_tp_intr_handler(hw);

	if (cause & MA_F)
		csio_ma_intr_handler(hw);

	if (cause & PM_TX_F)
		csio_pmtx_intr_handler(hw);

	if (cause & PM_RX_F)
		csio_pmrx_intr_handler(hw);

	if (cause & ULP_RX_F)
		csio_ulprx_intr_handler(hw);

	if (cause & CPL_SWITCH_F)
		csio_cplsw_intr_handler(hw);

	if (cause & SGE_F)
		csio_sge_intr_handler(hw);

	if (cause & ULP_TX_F)
		csio_ulptx_intr_handler(hw);

	/* Clear the interrupts just processed for which we are the master. */
	csio_wr_reg32(hw, cause & CSIO_GLBL_INTR_MASK, PL_INT_CAUSE_A);
	csio_rd_reg32(hw, PL_INT_CAUSE_A); /* flush */

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
csio_hw_mb_timer(struct timer_list *t)
{
	struct csio_mbm *mbm = from_timer(mbm, t, timer);
	struct csio_hw *hw = mbm->hw;
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
	} else if (op == CPL_FW6_MSG || op == CPL_FW4_MSG) {

		CSIO_INC_STATS(hw, n_cpl_fw6_msg);
		/* skip RSS header */
		msg = (void *)((uintptr_t)wr + sizeof(__be64));
		msg_len = (op == CPL_FW6_MSG) ? sizeof(struct cpl_fw6_msg) :
			   sizeof(struct cpl_fw4_msg);
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
csio_mgmt_tmo_handler(struct timer_list *t)
{
	struct csio_mgmtm *mgmtm = from_timer(mgmtm, t, mgmt_timer);
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
	timer_setup(&mgmtm->mgmt_timer, csio_mgmt_tmo_handler, 0);

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
	else if (csio_match_state(hw, csio_hws_uninit))
		return -EINVAL;
	else
		return -ENODEV;
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
	hw->chip_id = (hw->params.pci.device_id & CSIO_HW_CHIP_MASK);

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

		if (prot_type == CSIO_T5_FCOE_ASIC) {
			memcpy(hw->hw_ver,
			       csio_t5_fcoe_adapters[adap_type].model_no, 16);
			memcpy(hw->model_desc,
			       csio_t5_fcoe_adapters[adap_type].description,
			       32);
		} else {
			char tempName[32] = "Chelsio FCoE Controller";
			memcpy(hw->model_desc, tempName, 32);
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

	/* Initialize the HW chip ops T5 specific ops */
	hw->chip_ops = &t5_ops;

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
			rv = -ENOMEM;
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
