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

/*
 * Return the specified PCI-E Configuration Space register from our Physical
 * Function.  We try first via a Firmware LDST Command since we prefer to let
 * the firmware own all of these registers, but if that fails we go for it
 * directly ourselves.
 */
static uint32_t
csio_t4_read_pcie_cfg4(struct csio_hw *hw, int reg)
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
}

static int
csio_t4_set_mem_win(struct csio_hw *hw, uint32_t win)
{
	u32 bar0;
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
	bar0 = csio_t4_read_pcie_cfg4(hw, PCI_BASE_ADDRESS_0);
	bar0 &= PCI_BASE_ADDRESS_MEM_MASK;

	mem_win_base = bar0 + MEMWIN_BASE;

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
csio_t4_pcie_intr_handler(struct csio_hw *hw)
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
 * csio_t4_flash_cfg_addr - return the address of the flash configuration file
 * @hw: the HW module
 *
 * Return the address within the flash where the Firmware Configuration
 * File is stored.
 */
static unsigned int
csio_t4_flash_cfg_addr(struct csio_hw *hw)
{
	return FLASH_CFG_OFFSET;
}

/*
 *      csio_t4_mc_read - read from MC through backdoor accesses
 *      @hw: the hw module
 *      @idx: not used for T4 adapter
 *      @addr: address of first byte requested
 *      @data: 64 bytes of data containing the requested address
 *      @ecc: where to store the corresponding 64-bit ECC word
 *
 *      Read 64 bytes of data from MC starting at a 64-byte-aligned address
 *      that covers the requested address @addr.  If @parity is not %NULL it
 *      is assigned the 64-bit ECC word for the read data.
 */
static int
csio_t4_mc_read(struct csio_hw *hw, int idx, uint32_t addr, __be32 *data,
		uint64_t *ecc)
{
	int i;

	if (csio_rd_reg32(hw, MC_BIST_CMD) & START_BIST)
		return -EBUSY;
	csio_wr_reg32(hw, addr & ~0x3fU, MC_BIST_CMD_ADDR);
	csio_wr_reg32(hw, 64, MC_BIST_CMD_LEN);
	csio_wr_reg32(hw, 0xc, MC_BIST_DATA_PATTERN);
	csio_wr_reg32(hw, BIST_OPCODE(1) | START_BIST | BIST_CMD_GAP(1),
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
 *      csio_t4_edc_read - read from EDC through backdoor accesses
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
csio_t4_edc_read(struct csio_hw *hw, int idx, uint32_t addr, __be32 *data,
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
 * csio_t4_memory_rw - read/write EDC 0, EDC 1 or MC via PCIE memory window
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
csio_t4_memory_rw(struct csio_hw *hw, u32 win, int mtype, u32 addr,
		u32 len, uint32_t *buf, int dir)
{
	u32 pos, start, offset, memoffset, bar0;
	u32 edc_size, mc_size, mem_reg, mem_aperture, mem_base;

	/*
	 * Argument sanity checks ...
	 */
	if ((addr & 0x3) || (len & 0x3))
		return -EINVAL;

	/* Offset into the region of memory which is being accessed
	 * MEM_EDC0 = 0
	 * MEM_EDC1 = 1
	 * MEM_MC   = 2 -- T4
	 */
	edc_size  = EDRAM_SIZE_GET(csio_rd_reg32(hw, MA_EDRAM0_BAR));
	if (mtype != MEM_MC1)
		memoffset = (mtype * (edc_size * 1024 * 1024));
	else {
		mc_size = EXT_MEM_SIZE_GET(csio_rd_reg32(hw,
							 MA_EXT_MEMORY_BAR));
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

	bar0 = csio_t4_read_pcie_cfg4(hw, PCI_BASE_ADDRESS_0);
	bar0 &= PCI_BASE_ADDRESS_MEM_MASK;
	mem_base -= bar0;

	start = addr & ~(mem_aperture-1);
	offset = addr - start;

	csio_dbg(hw, "csio_t4_memory_rw: mem_reg: 0x%x, mem_aperture: 0x%x\n",
		 mem_reg, mem_aperture);
	csio_dbg(hw, "csio_t4_memory_rw: mem_base: 0x%x, mem_offset: 0x%x\n",
		 mem_base, memoffset);
	csio_dbg(hw, "csio_t4_memory_rw: bar0: 0x%x, start:0x%x, offset:0x%x\n",
		 bar0, start, offset);
	csio_dbg(hw, "csio_t4_memory_rw: mtype: %d, addr: 0x%x, len: %d\n",
		 mtype, addr, len);

	for (pos = start; len > 0; pos += mem_aperture, offset = 0) {
		/*
		 * Move PCI-E Memory Window to our current transfer
		 * position.  Read it back to ensure that changes propagate
		 * before we attempt to use the new value.
		 */
		csio_wr_reg32(hw, pos,
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
 * csio_t4_dfs_create_ext_mem - setup debugfs for MC to read the values
 * @hw: the csio_hw
 *
 * This function creates files in the debugfs with external memory region MC.
 */
static void
csio_t4_dfs_create_ext_mem(struct csio_hw *hw)
{
	u32 size;
	int i = csio_rd_reg32(hw, MA_TARGET_MEM_ENABLE);
	if (i & EXT_MEM_ENABLE) {
		size = csio_rd_reg32(hw, MA_EXT_MEMORY_BAR);
		csio_add_debugfs_mem(hw, "mc", MEM_MC,
				     EXT_MEM_SIZE_GET(size));
	}
}

/* T4 adapter specific function */
struct csio_hw_chip_ops t4_ops = {
	.chip_set_mem_win		= csio_t4_set_mem_win,
	.chip_pcie_intr_handler		= csio_t4_pcie_intr_handler,
	.chip_flash_cfg_addr		= csio_t4_flash_cfg_addr,
	.chip_mc_read			= csio_t4_mc_read,
	.chip_edc_read			= csio_t4_edc_read,
	.chip_memory_rw			= csio_t4_memory_rw,
	.chip_dfs_create_ext_mem	= csio_t4_dfs_create_ext_mem,
};
