/*
 * This file is part of the Chelsio FCoE driver for Linux.
 *
 * Copyright (c) 2008-2013 Chelsio Communications, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
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

#include "csio_hw.h"
#include "csio_init.h"

static int
csio_t5_set_mem_win(struct csio_hw *hw, uint32_t win)
{
	u32 mem_win_base;
	/*
	 * Truncation intentional: we only read the bottom 32-bits of the
	 * 64-bit BAR0/BAR1 ...  We use the hardware backdoor mechanism to
	 * read BAR0 instead of using pci_resource_start() because we could be
	 * operating from within a Virtual Machine which is trapping our
	 * accesses to our Configuration Space and we need to set up the PCI-E
	 * Memory Window decoders with the actual addresses which will be
	 * coming across the PCI-E link.
	 */

	/* For T5, only relative offset inside the PCIe BAR is passed */
	mem_win_base = MEMWIN_BASE;

	/*
	 * Set up memory window for accessing adapter memory ranges.  (Read
	 * back MA register to ensure that changes propagate before we attempt
	 * to use the new values.)
	 */
	csio_wr_reg32(hw, mem_win_base | BIR(0) |
			  WINDOW(ilog2(MEMWIN_APERTURE) - 10),
			  PCIE_MEM_ACCESS_REG(PCIE_MEM_ACCESS_BASE_WIN, win));
	csio_rd_reg32(hw,
		      PCIE_MEM_ACCESS_REG(PCIE_MEM_ACCESS_BASE_WIN, win));

	return 0;
}

/*
 * Interrupt handler for the PCIE module.
 */
static void
csio_t5_pcie_intr_handler(struct csio_hw *hw)
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
		{ IPRXDATAGRPPERR, "PCI IP Rx data group parity error",
		-1, 1 },
		{ RPLPERR, "PCI IP replay buffer parity error", -1, 1 },
		{ IPSOTPERR, "PCI IP SOT buffer parity error", -1, 1 },
		{ TRGT1GRPPERR, "PCI TRGT1 group FIFOs parity error", -1, 1 },
		{ READRSPERR, "Outbound read error", -1, 0 },
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
 * csio_t5_flash_cfg_addr - return the address of the flash configuration file
 * @hw: the HW module
 *
 * Return the address within the flash where the Firmware Configuration
 * File is stored.
 */
static unsigned int
csio_t5_flash_cfg_addr(struct csio_hw *hw)
{
	return FLASH_CFG_START;
}

/*
 *      csio_t5_mc_read - read from MC through backdoor accesses
 *      @hw: the hw module
 *      @idx: index to the register
 *      @addr: address of first byte requested
 *      @data: 64 bytes of data containing the requested address
 *      @ecc: where to store the corresponding 64-bit ECC word
 *
 *      Read 64 bytes of data from MC starting at a 64-byte-aligned address
 *      that covers the requested address @addr.  If @parity is not %NULL it
 *      is assigned the 64-bit ECC word for the read data.
 */
static int
csio_t5_mc_read(struct csio_hw *hw, int idx, uint32_t addr, __be32 *data,
		uint64_t *ecc)
{
	int i;
	uint32_t mc_bist_cmd_reg, mc_bist_cmd_addr_reg, mc_bist_cmd_len_reg;
	uint32_t mc_bist_status_rdata_reg, mc_bist_data_pattern_reg;

	mc_bist_cmd_reg = MC_REG(MC_P_BIST_CMD, idx);
	mc_bist_cmd_addr_reg = MC_REG(MC_P_BIST_CMD_ADDR, idx);
	mc_bist_cmd_len_reg = MC_REG(MC_P_BIST_CMD_LEN, idx);
	mc_bist_status_rdata_reg = MC_REG(MC_P_BIST_STATUS_RDATA, idx);
	mc_bist_data_pattern_reg = MC_REG(MC_P_BIST_DATA_PATTERN, idx);

	if (csio_rd_reg32(hw, mc_bist_cmd_reg) & START_BIST)
		return -EBUSY;
	csio_wr_reg32(hw, addr & ~0x3fU, mc_bist_cmd_addr_reg);
	csio_wr_reg32(hw, 64, mc_bist_cmd_len_reg);
	csio_wr_reg32(hw, 0xc, mc_bist_data_pattern_reg);
	csio_wr_reg32(hw, BIST_OPCODE(1) | START_BIST |  BIST_CMD_GAP(1),
		      mc_bist_cmd_reg);
	i = csio_hw_wait_op_done_val(hw, mc_bist_cmd_reg, START_BIST,
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
 *      csio_t5_edc_read - read from EDC through backdoor accesses
 *      @hw: the hw module
 *      @idx: which EDC to access
 *      @addr: address of first byte requested
 *      @data: 64 bytes of data containing the requested address
 *      @ecc: where to store the corresponding 64-bit ECC word
 *
 *      Read 64 bytes of data from EDC starting at a 64-byte-aligned address
 *      that covers the requested address @addr.  If @parity is not %NULL it
 *      is assigned the 64-bit ECC word for the read data.
 */
static int
csio_t5_edc_read(struct csio_hw *hw, int idx, uint32_t addr, __be32 *data,
		uint64_t *ecc)
{
	int i;
	uint32_t edc_bist_cmd_reg, edc_bist_cmd_addr_reg, edc_bist_cmd_len_reg;
	uint32_t edc_bist_cmd_data_pattern, edc_bist_status_rdata_reg;

/*
 * These macro are missing in t4_regs.h file.
 */
#define EDC_STRIDE_T5 (EDC_T51_BASE_ADDR - EDC_T50_BASE_ADDR)
#define EDC_REG_T5(reg, idx) (reg + EDC_STRIDE_T5 * idx)

	edc_bist_cmd_reg = EDC_REG_T5(EDC_H_BIST_CMD, idx);
	edc_bist_cmd_addr_reg = EDC_REG_T5(EDC_H_BIST_CMD_ADDR, idx);
	edc_bist_cmd_len_reg = EDC_REG_T5(EDC_H_BIST_CMD_LEN, idx);
	edc_bist_cmd_data_pattern = EDC_REG_T5(EDC_H_BIST_DATA_PATTERN, idx);
	edc_bist_status_rdata_reg = EDC_REG_T5(EDC_H_BIST_STATUS_RDATA, idx);
#undef EDC_REG_T5
#undef EDC_STRIDE_T5

	if (csio_rd_reg32(hw, edc_bist_cmd_reg) & START_BIST)
		return -EBUSY;
	csio_wr_reg32(hw, addr & ~0x3fU, edc_bist_cmd_addr_reg);
	csio_wr_reg32(hw, 64, edc_bist_cmd_len_reg);
	csio_wr_reg32(hw, 0xc, edc_bist_cmd_data_pattern);
	csio_wr_reg32(hw, BIST_OPCODE(1) | START_BIST |  BIST_CMD_GAP(1),
		      edc_bist_cmd_reg);
	i = csio_hw_wait_op_done_val(hw, edc_bist_cmd_reg, START_BIST,
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
 * csio_t5_memory_rw - read/write EDC 0, EDC 1 or MC via PCIE memory window
 * @hw: the csio_hw
 * @win: PCI-E memory Window to use
 * @mtype: memory type: MEM_EDC0, MEM_EDC1, MEM_MC0 (or MEM_MC) or MEM_MC1
 * @addr: address within indicated memory type
 * @len: amount of memory to transfer
 * @buf: host memory buffer
 * @dir: direction of transfer 1 => read, 0 => write
 *
 * Reads/writes an [almost] arbitrary memory region in the firmware: the
 * firmware memory address, length and host buffer must be aligned on
 * 32-bit boudaries.  The memory is transferred as a raw byte sequence
 * from/to the firmware's memory.  If this memory contains data
 * structures which contain multi-byte integers, it's the callers
 * responsibility to perform appropriate byte order conversions.
 */
static int
csio_t5_memory_rw(struct csio_hw *hw, u32 win, int mtype, u32 addr,
		u32 len, uint32_t *buf, int dir)
{
	u32 pos, start, offset, memoffset;
	u32 edc_size, mc_size, win_pf, mem_reg, mem_aperture, mem_base;

	/*
	 * Argument sanity checks ...
	 */
	if ((addr & 0x3) || (len & 0x3))
		return -EINVAL;

	/* Offset into the region of memory which is being accessed
	 * MEM_EDC0 = 0
	 * MEM_EDC1 = 1
	 * MEM_MC   = 2 -- T4
	 * MEM_MC0  = 2 -- For T5
	 * MEM_MC1  = 3 -- For T5
	 */
	edc_size  = EDRAM0_SIZE_G(csio_rd_reg32(hw, MA_EDRAM0_BAR_A));
	if (mtype != MEM_MC1)
		memoffset = (mtype * (edc_size * 1024 * 1024));
	else {
		mc_size = EXT_MEM_SIZE_G(csio_rd_reg32(hw,
						       MA_EXT_MEMORY_BAR_A));
		memoffset = (MEM_MC0 * edc_size + mc_size) * 1024 * 1024;
	}

	/* Determine the PCIE_MEM_ACCESS_OFFSET */
	addr = addr + memoffset;

	/*
	 * Each PCI-E Memory Window is programmed with a window size -- or
	 * "aperture" -- which controls the granularity of its mapping onto
	 * adapter memory.  We need to grab that aperture in order to know
	 * how to use the specified window.  The window is also programmed
	 * with the base address of the Memory Window in BAR0's address
	 * space.  For T4 this is an absolute PCI-E Bus Address.  For T5
	 * the address is relative to BAR0.
	 */
	mem_reg = csio_rd_reg32(hw,
			PCIE_MEM_ACCESS_REG(PCIE_MEM_ACCESS_BASE_WIN, win));
	mem_aperture = 1 << (WINDOW(mem_reg) + 10);
	mem_base = GET_PCIEOFST(mem_reg) << 10;

	start = addr & ~(mem_aperture-1);
	offset = addr - start;
	win_pf = V_PFNUM(hw->pfn);

	csio_dbg(hw, "csio_t5_memory_rw: mem_reg: 0x%x, mem_aperture: 0x%x\n",
		 mem_reg, mem_aperture);
	csio_dbg(hw, "csio_t5_memory_rw: mem_base: 0x%x, mem_offset: 0x%x\n",
		 mem_base, memoffset);
	csio_dbg(hw, "csio_t5_memory_rw: start:0x%x, offset:0x%x, win_pf:%d\n",
		 start, offset, win_pf);
	csio_dbg(hw, "csio_t5_memory_rw: mtype: %d, addr: 0x%x, len: %d\n",
		 mtype, addr, len);

	for (pos = start; len > 0; pos += mem_aperture, offset = 0) {
		/*
		 * Move PCI-E Memory Window to our current transfer
		 * position.  Read it back to ensure that changes propagate
		 * before we attempt to use the new value.
		 */
		csio_wr_reg32(hw, pos | win_pf,
			PCIE_MEM_ACCESS_REG(PCIE_MEM_ACCESS_OFFSET, win));
		csio_rd_reg32(hw,
			PCIE_MEM_ACCESS_REG(PCIE_MEM_ACCESS_OFFSET, win));

		while (offset < mem_aperture && len > 0) {
			if (dir)
				*buf++ = csio_rd_reg32(hw, mem_base + offset);
			else
				csio_wr_reg32(hw, *buf++, mem_base + offset);

			offset += sizeof(__be32);
			len -= sizeof(__be32);
		}
	}
	return 0;
}

/*
 * csio_t5_dfs_create_ext_mem - setup debugfs for MC0 or MC1 to read the values
 * @hw: the csio_hw
 *
 * This function creates files in the debugfs with external memory region
 * MC0 & MC1.
 */
static void
csio_t5_dfs_create_ext_mem(struct csio_hw *hw)
{
	u32 size;
	int i = csio_rd_reg32(hw, MA_TARGET_MEM_ENABLE_A);

	if (i & EXT_MEM_ENABLE_F) {
		size = csio_rd_reg32(hw, MA_EXT_MEMORY_BAR_A);
		csio_add_debugfs_mem(hw, "mc0", MEM_MC0,
				     EXT_MEM_SIZE_G(size));
	}
	if (i & EXT_MEM1_ENABLE_F) {
		size = csio_rd_reg32(hw, MA_EXT_MEMORY1_BAR_A);
		csio_add_debugfs_mem(hw, "mc1", MEM_MC1,
				     EXT_MEM_SIZE_G(size));
	}
}

/* T5 adapter specific function */
struct csio_hw_chip_ops t5_ops = {
	.chip_set_mem_win		= csio_t5_set_mem_win,
	.chip_pcie_intr_handler		= csio_t5_pcie_intr_handler,
	.chip_flash_cfg_addr		= csio_t5_flash_cfg_addr,
	.chip_mc_read			= csio_t5_mc_read,
	.chip_edc_read			= csio_t5_edc_read,
	.chip_memory_rw			= csio_t5_memory_rw,
	.chip_dfs_create_ext_mem	= csio_t5_dfs_create_ext_mem,
};
