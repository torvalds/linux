/***************************************************************************
 * Copyright (c) 2005-2009, Broadcom Corporation.
 *
 *  Name: crystalhd_hw . c
 *
 *  Description:
 *		BCM70010 Linux driver HW layer.
 *
 **********************************************************************
 * This file is part of the crystalhd device driver.
 *
 * This driver is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * This driver is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this driver.  If not, see <http://www.gnu.org/licenses/>.
 **********************************************************************/

#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include "crystalhd_hw.h"

/* Functions internal to this file */

static void crystalhd_enable_uarts(struct crystalhd_adp *adp)
{
	bc_dec_reg_wr(adp, UartSelectA, BSVS_UART_STREAM);
	bc_dec_reg_wr(adp, UartSelectB, BSVS_UART_DEC_OUTER);
}


static void crystalhd_start_dram(struct crystalhd_adp *adp)
{
	bc_dec_reg_wr(adp, SDRAM_PARAM, ((40 / 5 - 1) <<  0) |
	/* tras (40ns tras)/(5ns period) -1 ((15/5 - 1) <<  4) | // trcd */
		      ((15 / 5 - 1) <<  7) |	/* trp */
		      ((10 / 5 - 1) << 10) |	/* trrd */
		      ((15 / 5 + 1) << 12) |	/* twr */
		      ((2 + 1) << 16) |		/* twtr */
		      ((70 / 5 - 2) << 19) |	/* trfc */
		      (0 << 23));

	bc_dec_reg_wr(adp, SDRAM_PRECHARGE, 0);
	bc_dec_reg_wr(adp, SDRAM_EXT_MODE, 2);
	bc_dec_reg_wr(adp, SDRAM_MODE, 0x132);
	bc_dec_reg_wr(adp, SDRAM_PRECHARGE, 0);
	bc_dec_reg_wr(adp, SDRAM_REFRESH, 0);
	bc_dec_reg_wr(adp, SDRAM_REFRESH, 0);
	bc_dec_reg_wr(adp, SDRAM_MODE, 0x32);
	/* setting the refresh rate here */
	bc_dec_reg_wr(adp, SDRAM_REF_PARAM, ((1 << 12) | 96));
}


static bool crystalhd_bring_out_of_rst(struct crystalhd_adp *adp)
{
	union link_misc_perst_deco_ctrl rst_deco_cntrl;
	union link_misc_perst_clk_ctrl rst_clk_cntrl;
	uint32_t temp;

	/*
	 * Link clocks: MISC_PERST_CLOCK_CTRL Clear PLL power down bit,
	 * delay to allow PLL to lock Clear alternate clock, stop clock bits
	 */
	rst_clk_cntrl.whole_reg = crystalhd_reg_rd(adp, MISC_PERST_CLOCK_CTRL);
	rst_clk_cntrl.pll_pwr_dn = 0;
	crystalhd_reg_wr(adp, MISC_PERST_CLOCK_CTRL, rst_clk_cntrl.whole_reg);
	msleep_interruptible(50);

	rst_clk_cntrl.whole_reg = crystalhd_reg_rd(adp, MISC_PERST_CLOCK_CTRL);
	rst_clk_cntrl.stop_core_clk = 0;
	rst_clk_cntrl.sel_alt_clk = 0;

	crystalhd_reg_wr(adp, MISC_PERST_CLOCK_CTRL, rst_clk_cntrl.whole_reg);
	msleep_interruptible(50);

	/*
	 * Bus Arbiter Timeout: GISB_ARBITER_TIMER
	 * Set internal bus arbiter timeout to 40us based on core clock speed
	 * (63MHz * 40us = 0x9D8)
	 */
	crystalhd_reg_wr(adp, GISB_ARBITER_TIMER, 0x9D8);

	/*
	 * Decoder clocks: MISC_PERST_DECODER_CTRL
	 * Enable clocks while 7412 reset is asserted, delay
	 * De-assert 7412 reset
	 */
	rst_deco_cntrl.whole_reg = crystalhd_reg_rd(adp, MISC_PERST_DECODER_CTRL);
	rst_deco_cntrl.stop_bcm_7412_clk = 0;
	rst_deco_cntrl.bcm7412_rst = 1;
	crystalhd_reg_wr(adp, MISC_PERST_DECODER_CTRL, rst_deco_cntrl.whole_reg);
	msleep_interruptible(10);

	rst_deco_cntrl.whole_reg = crystalhd_reg_rd(adp, MISC_PERST_DECODER_CTRL);
	rst_deco_cntrl.bcm7412_rst = 0;
	crystalhd_reg_wr(adp, MISC_PERST_DECODER_CTRL, rst_deco_cntrl.whole_reg);
	msleep_interruptible(50);

	/* Disable OTP_CONTENT_MISC to 0 to disable all secure modes */
	crystalhd_reg_wr(adp, OTP_CONTENT_MISC, 0);

	/* Clear bit 29 of 0x404 */
	temp = crystalhd_reg_rd(adp, PCIE_TL_TRANSACTION_CONFIGURATION);
	temp &= ~BC_BIT(29);
	crystalhd_reg_wr(adp, PCIE_TL_TRANSACTION_CONFIGURATION, temp);

	/* 2.5V regulator must be set to 2.6 volts (+6%) */
	/* FIXME: jarod: what's the point of this reg read? */
	temp = crystalhd_reg_rd(adp, MISC_PERST_VREG_CTRL);
	crystalhd_reg_wr(adp, MISC_PERST_VREG_CTRL, 0xF3);

	return true;
}

static bool crystalhd_put_in_reset(struct crystalhd_adp *adp)
{
	union link_misc_perst_deco_ctrl rst_deco_cntrl;
	union link_misc_perst_clk_ctrl  rst_clk_cntrl;
	uint32_t                  temp;

	/*
	 * Decoder clocks: MISC_PERST_DECODER_CTRL
	 * Assert 7412 reset, delay
	 * Assert 7412 stop clock
	 */
	rst_deco_cntrl.whole_reg = crystalhd_reg_rd(adp, MISC_PERST_DECODER_CTRL);
	rst_deco_cntrl.stop_bcm_7412_clk = 1;
	crystalhd_reg_wr(adp, MISC_PERST_DECODER_CTRL, rst_deco_cntrl.whole_reg);
	msleep_interruptible(50);

	/* Bus Arbiter Timeout: GISB_ARBITER_TIMER
	 * Set internal bus arbiter timeout to 40us based on core clock speed
	 * (6.75MHZ * 40us = 0x10E)
	 */
	crystalhd_reg_wr(adp, GISB_ARBITER_TIMER, 0x10E);

	/* Link clocks: MISC_PERST_CLOCK_CTRL
	 * Stop core clk, delay
	 * Set alternate clk, delay, set PLL power down
	 */
	rst_clk_cntrl.whole_reg = crystalhd_reg_rd(adp, MISC_PERST_CLOCK_CTRL);
	rst_clk_cntrl.stop_core_clk = 1;
	rst_clk_cntrl.sel_alt_clk = 1;
	crystalhd_reg_wr(adp, MISC_PERST_CLOCK_CTRL, rst_clk_cntrl.whole_reg);
	msleep_interruptible(50);

	rst_clk_cntrl.whole_reg = crystalhd_reg_rd(adp, MISC_PERST_CLOCK_CTRL);
	rst_clk_cntrl.pll_pwr_dn = 1;
	crystalhd_reg_wr(adp, MISC_PERST_CLOCK_CTRL, rst_clk_cntrl.whole_reg);

	/*
	 * Read and restore the Transaction Configuration Register
	 * after core reset
	 */
	temp = crystalhd_reg_rd(adp, PCIE_TL_TRANSACTION_CONFIGURATION);

	/*
	 * Link core soft reset: MISC3_RESET_CTRL
	 * - Write BIT[0]=1 and read it back for core reset to take place
	 */
	crystalhd_reg_wr(adp, MISC3_RESET_CTRL, 1);
	rst_deco_cntrl.whole_reg = crystalhd_reg_rd(adp, MISC3_RESET_CTRL);
	msleep_interruptible(50);

	/* restore the transaction configuration register */
	crystalhd_reg_wr(adp, PCIE_TL_TRANSACTION_CONFIGURATION, temp);

	return true;
}

static void crystalhd_disable_interrupts(struct crystalhd_adp *adp)
{
	union intr_mask_reg   intr_mask;
	intr_mask.whole_reg = crystalhd_reg_rd(adp, INTR_INTR_MSK_STS_REG);
	intr_mask.mask_pcie_err = 1;
	intr_mask.mask_pcie_rbusmast_err = 1;
	intr_mask.mask_pcie_rgr_bridge   = 1;
	intr_mask.mask_rx_done = 1;
	intr_mask.mask_rx_err  = 1;
	intr_mask.mask_tx_done = 1;
	intr_mask.mask_tx_err  = 1;
	crystalhd_reg_wr(adp, INTR_INTR_MSK_SET_REG, intr_mask.whole_reg);

	return;
}

static void crystalhd_enable_interrupts(struct crystalhd_adp *adp)
{
	union intr_mask_reg   intr_mask;
	intr_mask.whole_reg = crystalhd_reg_rd(adp, INTR_INTR_MSK_STS_REG);
	intr_mask.mask_pcie_err = 1;
	intr_mask.mask_pcie_rbusmast_err = 1;
	intr_mask.mask_pcie_rgr_bridge   = 1;
	intr_mask.mask_rx_done = 1;
	intr_mask.mask_rx_err  = 1;
	intr_mask.mask_tx_done = 1;
	intr_mask.mask_tx_err  = 1;
	crystalhd_reg_wr(adp, INTR_INTR_MSK_CLR_REG, intr_mask.whole_reg);

	return;
}

static void crystalhd_clear_errors(struct crystalhd_adp *adp)
{
	uint32_t reg;

	/* FIXME: jarod: wouldn't we want to write a 0 to the reg? Or does the write clear the bits specified? */
	reg = crystalhd_reg_rd(adp, MISC1_Y_RX_ERROR_STATUS);
	if (reg)
		crystalhd_reg_wr(adp, MISC1_Y_RX_ERROR_STATUS, reg);

	reg = crystalhd_reg_rd(adp, MISC1_UV_RX_ERROR_STATUS);
	if (reg)
		crystalhd_reg_wr(adp, MISC1_UV_RX_ERROR_STATUS, reg);

	reg = crystalhd_reg_rd(adp, MISC1_TX_DMA_ERROR_STATUS);
	if (reg)
		crystalhd_reg_wr(adp, MISC1_TX_DMA_ERROR_STATUS, reg);
}

static void crystalhd_clear_interrupts(struct crystalhd_adp *adp)
{
	uint32_t intr_sts = crystalhd_reg_rd(adp, INTR_INTR_STATUS);

	if (intr_sts) {
		crystalhd_reg_wr(adp, INTR_INTR_CLR_REG, intr_sts);

		/* Write End Of Interrupt for PCIE */
		crystalhd_reg_wr(adp, INTR_EOI_CTRL, 1);
	}
}

static void crystalhd_soft_rst(struct crystalhd_adp *adp)
{
	uint32_t val;

	/* Assert c011 soft reset*/
	bc_dec_reg_wr(adp, DecHt_HostSwReset, 0x00000001);
	msleep_interruptible(50);

	/* Release c011 soft reset*/
	bc_dec_reg_wr(adp, DecHt_HostSwReset, 0x00000000);

	/* Disable Stuffing..*/
	val = crystalhd_reg_rd(adp, MISC2_GLOBAL_CTRL);
	val |= BC_BIT(8);
	crystalhd_reg_wr(adp, MISC2_GLOBAL_CTRL, val);
}

static bool crystalhd_load_firmware_config(struct crystalhd_adp *adp)
{
	uint32_t i = 0, reg;

	crystalhd_reg_wr(adp, DCI_DRAM_BASE_ADDR, (BC_DRAM_FW_CFG_ADDR >> 19));

	crystalhd_reg_wr(adp, AES_CMD, 0);
	crystalhd_reg_wr(adp, AES_CONFIG_INFO, (BC_DRAM_FW_CFG_ADDR & 0x7FFFF));
	crystalhd_reg_wr(adp, AES_CMD, 0x1);

	/* FIXME: jarod: I've seen this fail, and introducing extra delays helps... */
	for (i = 0; i < 100; ++i) {
		reg = crystalhd_reg_rd(adp, AES_STATUS);
		if (reg & 0x1)
			return true;
		msleep_interruptible(10);
	}

	return false;
}


static bool crystalhd_start_device(struct crystalhd_adp *adp)
{
	uint32_t dbg_options, glb_cntrl = 0, reg_pwrmgmt = 0;

	BCMLOG(BCMLOG_INFO, "Starting BCM70012 Device\n");

	reg_pwrmgmt = crystalhd_reg_rd(adp, PCIE_DLL_DATA_LINK_CONTROL);
	reg_pwrmgmt &= ~ASPM_L1_ENABLE;

	crystalhd_reg_wr(adp, PCIE_DLL_DATA_LINK_CONTROL, reg_pwrmgmt);

	if (!crystalhd_bring_out_of_rst(adp)) {
		BCMLOG_ERR("Failed To Bring Link Out Of Reset\n");
		return false;
	}

	crystalhd_disable_interrupts(adp);

	crystalhd_clear_errors(adp);

	crystalhd_clear_interrupts(adp);

	crystalhd_enable_interrupts(adp);

	/* Enable the option for getting the total no. of DWORDS
	 * that have been transfered by the RXDMA engine
	 */
	dbg_options = crystalhd_reg_rd(adp, MISC1_DMA_DEBUG_OPTIONS_REG);
	dbg_options |= 0x10;
	crystalhd_reg_wr(adp, MISC1_DMA_DEBUG_OPTIONS_REG, dbg_options);

	/* Enable PCI Global Control options */
	glb_cntrl = crystalhd_reg_rd(adp, MISC2_GLOBAL_CTRL);
	glb_cntrl |= 0x100;
	glb_cntrl |= 0x8000;
	crystalhd_reg_wr(adp, MISC2_GLOBAL_CTRL, glb_cntrl);

	crystalhd_enable_interrupts(adp);

	crystalhd_soft_rst(adp);
	crystalhd_start_dram(adp);
	crystalhd_enable_uarts(adp);

	return true;
}

static bool crystalhd_stop_device(struct crystalhd_adp *adp)
{
	uint32_t reg;

	BCMLOG(BCMLOG_INFO, "Stopping BCM70012 Device\n");
	/* Clear and disable interrupts */
	crystalhd_disable_interrupts(adp);
	crystalhd_clear_errors(adp);
	crystalhd_clear_interrupts(adp);

	if (!crystalhd_put_in_reset(adp))
		BCMLOG_ERR("Failed to Put Link To Reset State\n");

	reg = crystalhd_reg_rd(adp, PCIE_DLL_DATA_LINK_CONTROL);
	reg |= ASPM_L1_ENABLE;
	crystalhd_reg_wr(adp, PCIE_DLL_DATA_LINK_CONTROL, reg);

	/* Set PCI Clk Req */
	reg = crystalhd_reg_rd(adp, PCIE_CLK_REQ_REG);
	reg |= PCI_CLK_REQ_ENABLE;
	crystalhd_reg_wr(adp, PCIE_CLK_REQ_REG, reg);

	return true;
}

static struct crystalhd_rx_dma_pkt *crystalhd_hw_alloc_rx_pkt(struct crystalhd_hw *hw)
{
	unsigned long flags = 0;
	struct crystalhd_rx_dma_pkt *temp = NULL;

	if (!hw)
		return NULL;

	spin_lock_irqsave(&hw->lock, flags);
	temp = hw->rx_pkt_pool_head;
	if (temp) {
		hw->rx_pkt_pool_head = hw->rx_pkt_pool_head->next;
		temp->dio_req = NULL;
		temp->pkt_tag = 0;
		temp->flags = 0;
	}
	spin_unlock_irqrestore(&hw->lock, flags);

	return temp;
}

static void crystalhd_hw_free_rx_pkt(struct crystalhd_hw *hw,
				   struct crystalhd_rx_dma_pkt *pkt)
{
	unsigned long flags = 0;

	if (!hw || !pkt)
		return;

	spin_lock_irqsave(&hw->lock, flags);
	pkt->next = hw->rx_pkt_pool_head;
	hw->rx_pkt_pool_head = pkt;
	spin_unlock_irqrestore(&hw->lock, flags);
}

/*
 * Call back from TX - IOQ deletion.
 *
 * This routine will release the TX DMA rings allocated
 * druing setup_dma rings interface.
 *
 * Memory is allocated per DMA ring basis. This is just
 * a place holder to be able to create the dio queues.
 */
static void crystalhd_tx_desc_rel_call_back(void *context, void *data)
{
}

/*
 * Rx Packet release callback..
 *
 * Release All user mapped capture buffers and Our DMA packets
 * back to our free pool. The actual cleanup of the DMA
 * ring descriptors happen during dma ring release.
 */
static void crystalhd_rx_pkt_rel_call_back(void *context, void *data)
{
	struct crystalhd_hw *hw = (struct crystalhd_hw *)context;
	struct crystalhd_rx_dma_pkt *pkt = (struct crystalhd_rx_dma_pkt *)data;

	if (!pkt || !hw) {
		BCMLOG_ERR("Invalid arg - %p %p\n", hw, pkt);
		return;
	}

	if (pkt->dio_req)
		crystalhd_unmap_dio(hw->adp, pkt->dio_req);
	else
		BCMLOG_ERR("Missing dio_req: 0x%x\n", pkt->pkt_tag);

	crystalhd_hw_free_rx_pkt(hw, pkt);
}

#define crystalhd_hw_delete_ioq(adp, q)		\
	if (q) {				\
		crystalhd_delete_dioq(adp, q);	\
		q = NULL;			\
	}

static void crystalhd_hw_delete_ioqs(struct crystalhd_hw *hw)
{
	if (!hw)
		return;

	BCMLOG(BCMLOG_DBG, "Deleting IOQs\n");
	crystalhd_hw_delete_ioq(hw->adp, hw->tx_actq);
	crystalhd_hw_delete_ioq(hw->adp, hw->tx_freeq);
	crystalhd_hw_delete_ioq(hw->adp, hw->rx_actq);
	crystalhd_hw_delete_ioq(hw->adp, hw->rx_freeq);
	crystalhd_hw_delete_ioq(hw->adp, hw->rx_rdyq);
}

#define crystalhd_hw_create_ioq(sts, hw, q, cb)			\
do {								\
	sts = crystalhd_create_dioq(hw->adp, &q, cb, hw);	\
	if (sts != BC_STS_SUCCESS)				\
		goto hw_create_ioq_err;				\
} while (0)

/*
 * Create IOQs..
 *
 * TX - Active & Free
 * RX - Active, Ready and Free.
 */
static enum BC_STATUS crystalhd_hw_create_ioqs(struct crystalhd_hw   *hw)
{
	enum BC_STATUS   sts = BC_STS_SUCCESS;

	if (!hw) {
		BCMLOG_ERR("Invalid Arg!!\n");
		return BC_STS_INV_ARG;
	}

	crystalhd_hw_create_ioq(sts, hw, hw->tx_freeq,
			      crystalhd_tx_desc_rel_call_back);
	crystalhd_hw_create_ioq(sts, hw, hw->tx_actq,
			      crystalhd_tx_desc_rel_call_back);

	crystalhd_hw_create_ioq(sts, hw, hw->rx_freeq,
			      crystalhd_rx_pkt_rel_call_back);
	crystalhd_hw_create_ioq(sts, hw, hw->rx_rdyq,
			      crystalhd_rx_pkt_rel_call_back);
	crystalhd_hw_create_ioq(sts, hw, hw->rx_actq,
			      crystalhd_rx_pkt_rel_call_back);

	return sts;

hw_create_ioq_err:
	crystalhd_hw_delete_ioqs(hw);

	return sts;
}


static bool crystalhd_code_in_full(struct crystalhd_adp *adp, uint32_t needed_sz,
				 bool b_188_byte_pkts,  uint8_t flags)
{
	uint32_t base, end, writep, readp;
	uint32_t cpbSize, cpbFullness, fifoSize;

	if (flags & 0x02) { /* ASF Bit is set */
		base   = bc_dec_reg_rd(adp, REG_Dec_TsAudCDB2Base);
		end    = bc_dec_reg_rd(adp, REG_Dec_TsAudCDB2End);
		writep = bc_dec_reg_rd(adp, REG_Dec_TsAudCDB2Wrptr);
		readp  = bc_dec_reg_rd(adp, REG_Dec_TsAudCDB2Rdptr);
	} else if (b_188_byte_pkts) { /*Encrypted 188 byte packets*/
		base   = bc_dec_reg_rd(adp, REG_Dec_TsUser0Base);
		end    = bc_dec_reg_rd(adp, REG_Dec_TsUser0End);
		writep = bc_dec_reg_rd(adp, REG_Dec_TsUser0Wrptr);
		readp  = bc_dec_reg_rd(adp, REG_Dec_TsUser0Rdptr);
	} else {
		base   = bc_dec_reg_rd(adp, REG_DecCA_RegCinBase);
		end    = bc_dec_reg_rd(adp, REG_DecCA_RegCinEnd);
		writep = bc_dec_reg_rd(adp, REG_DecCA_RegCinWrPtr);
		readp  = bc_dec_reg_rd(adp, REG_DecCA_RegCinRdPtr);
	}

	cpbSize = end - base;
	if (writep >= readp)
		cpbFullness = writep - readp;
	else
		cpbFullness = (end - base) - (readp - writep);

	fifoSize = cpbSize - cpbFullness;

	if (fifoSize < BC_INFIFO_THRESHOLD)
		return true;

	if (needed_sz > (fifoSize - BC_INFIFO_THRESHOLD))
		return true;

	return false;
}

static enum BC_STATUS crystalhd_hw_tx_req_complete(struct crystalhd_hw *hw,
					    uint32_t list_id, enum BC_STATUS cs)
{
	struct tx_dma_pkt *tx_req;

	if (!hw || !list_id) {
		BCMLOG_ERR("Invalid Arg..\n");
		return BC_STS_INV_ARG;
	}

	hw->pwr_lock--;

	tx_req = (struct tx_dma_pkt *)crystalhd_dioq_find_and_fetch(hw->tx_actq, list_id);
	if (!tx_req) {
		if (cs != BC_STS_IO_USER_ABORT)
			BCMLOG_ERR("Find and Fetch Did not find req\n");
		return BC_STS_NO_DATA;
	}

	if (tx_req->call_back) {
		tx_req->call_back(tx_req->dio_req, tx_req->cb_event, cs);
		tx_req->dio_req   = NULL;
		tx_req->cb_event  = NULL;
		tx_req->call_back = NULL;
	} else {
		BCMLOG(BCMLOG_DBG, "Missing Tx Callback - %X\n",
		       tx_req->list_tag);
	}

	/* Now put back the tx_list back in FreeQ */
	tx_req->list_tag = 0;

	return crystalhd_dioq_add(hw->tx_freeq, tx_req, false, 0);
}

static bool crystalhd_tx_list0_handler(struct crystalhd_hw *hw, uint32_t err_sts)
{
	uint32_t err_mask, tmp;
	unsigned long flags = 0;

	err_mask = MISC1_TX_DMA_ERROR_STATUS_TX_L0_DESC_TX_ABORT_ERRORS_MASK |
		MISC1_TX_DMA_ERROR_STATUS_TX_L0_DMA_DATA_TX_ABORT_ERRORS_MASK |
		MISC1_TX_DMA_ERROR_STATUS_TX_L0_FIFO_FULL_ERRORS_MASK;

	if (!(err_sts & err_mask))
		return false;

	BCMLOG_ERR("Error on Tx-L0 %x\n", err_sts);

	tmp = err_mask;

	if (err_sts & MISC1_TX_DMA_ERROR_STATUS_TX_L0_FIFO_FULL_ERRORS_MASK)
		tmp &= ~MISC1_TX_DMA_ERROR_STATUS_TX_L0_FIFO_FULL_ERRORS_MASK;

	if (tmp) {
		spin_lock_irqsave(&hw->lock, flags);
		/* reset list index.*/
		hw->tx_list_post_index = 0;
		spin_unlock_irqrestore(&hw->lock, flags);
	}

	tmp = err_sts & err_mask;
	crystalhd_reg_wr(hw->adp, MISC1_TX_DMA_ERROR_STATUS, tmp);

	return true;
}

static bool crystalhd_tx_list1_handler(struct crystalhd_hw *hw, uint32_t err_sts)
{
	uint32_t err_mask, tmp;
	unsigned long flags = 0;

	err_mask = MISC1_TX_DMA_ERROR_STATUS_TX_L1_DESC_TX_ABORT_ERRORS_MASK |
		MISC1_TX_DMA_ERROR_STATUS_TX_L1_DMA_DATA_TX_ABORT_ERRORS_MASK |
		MISC1_TX_DMA_ERROR_STATUS_TX_L1_FIFO_FULL_ERRORS_MASK;

	if (!(err_sts & err_mask))
		return false;

	BCMLOG_ERR("Error on Tx-L1 %x\n", err_sts);

	tmp = err_mask;

	if (err_sts & MISC1_TX_DMA_ERROR_STATUS_TX_L1_FIFO_FULL_ERRORS_MASK)
		tmp &= ~MISC1_TX_DMA_ERROR_STATUS_TX_L1_FIFO_FULL_ERRORS_MASK;

	if (tmp) {
		spin_lock_irqsave(&hw->lock, flags);
		/* reset list index.*/
		hw->tx_list_post_index = 0;
		spin_unlock_irqrestore(&hw->lock, flags);
	}

	tmp = err_sts & err_mask;
	crystalhd_reg_wr(hw->adp, MISC1_TX_DMA_ERROR_STATUS, tmp);

	return true;
}

static void crystalhd_tx_isr(struct crystalhd_hw *hw, uint32_t int_sts)
{
	uint32_t err_sts;

	if (int_sts & INTR_INTR_STATUS_L0_TX_DMA_DONE_INTR_MASK)
		crystalhd_hw_tx_req_complete(hw, hw->tx_ioq_tag_seed + 0,
					   BC_STS_SUCCESS);

	if (int_sts & INTR_INTR_STATUS_L1_TX_DMA_DONE_INTR_MASK)
		crystalhd_hw_tx_req_complete(hw, hw->tx_ioq_tag_seed + 1,
					   BC_STS_SUCCESS);

	if (!(int_sts & (INTR_INTR_STATUS_L0_TX_DMA_ERR_INTR_MASK |
			INTR_INTR_STATUS_L1_TX_DMA_ERR_INTR_MASK))) {
			/* No error mask set.. */
			return;
	}

	/* Handle Tx errors. */
	err_sts = crystalhd_reg_rd(hw->adp, MISC1_TX_DMA_ERROR_STATUS);

	if (crystalhd_tx_list0_handler(hw, err_sts))
		crystalhd_hw_tx_req_complete(hw, hw->tx_ioq_tag_seed + 0,
					   BC_STS_ERROR);

	if (crystalhd_tx_list1_handler(hw, err_sts))
		crystalhd_hw_tx_req_complete(hw, hw->tx_ioq_tag_seed + 1,
					   BC_STS_ERROR);

	hw->stats.tx_errors++;
}

static void crystalhd_hw_dump_desc(struct dma_descriptor *p_dma_desc,
				 uint32_t ul_desc_index, uint32_t cnt)
{
	uint32_t ix, ll = 0;

	if (!p_dma_desc || !cnt)
		return;

	/* FIXME: jarod: perhaps a modparam desc_debug to enable this, rather than
	 * setting ll (log level, I presume) to non-zero? */
	if (!ll)
		return;

	for (ix = ul_desc_index; ix < (ul_desc_index + cnt); ix++) {
		BCMLOG(ll, "%s[%d] Buff[%x:%x] Next:[%x:%x] XferSz:%x Intr:%x,Last:%x\n",
		       ((p_dma_desc[ul_desc_index].dma_dir) ? "TDesc" : "RDesc"),
		       ul_desc_index,
		       p_dma_desc[ul_desc_index].buff_addr_high,
		       p_dma_desc[ul_desc_index].buff_addr_low,
		       p_dma_desc[ul_desc_index].next_desc_addr_high,
		       p_dma_desc[ul_desc_index].next_desc_addr_low,
		       p_dma_desc[ul_desc_index].xfer_size,
		       p_dma_desc[ul_desc_index].intr_enable,
		       p_dma_desc[ul_desc_index].last_rec_indicator);
	}

}

static enum BC_STATUS crystalhd_hw_fill_desc(struct crystalhd_dio_req *ioreq,
				      struct dma_descriptor *desc,
				      dma_addr_t desc_paddr_base,
				      uint32_t sg_cnt, uint32_t sg_st_ix,
				      uint32_t sg_st_off, uint32_t xfr_sz)
{
	uint32_t count = 0, ix = 0, sg_ix = 0, len = 0, last_desc_ix = 0;
	dma_addr_t desc_phy_addr = desc_paddr_base;
	union addr_64 addr_temp;

	if (!ioreq || !desc || !desc_paddr_base || !xfr_sz ||
	    (!sg_cnt && !ioreq->uinfo.dir_tx)) {
		BCMLOG_ERR("Invalid Args\n");
		return BC_STS_INV_ARG;
	}

	for (ix = 0; ix < sg_cnt; ix++) {

		/* Setup SGLE index. */
		sg_ix = ix + sg_st_ix;

		/* Get SGLE length */
		len = crystalhd_get_sgle_len(ioreq, sg_ix);
		if (len % 4) {
			BCMLOG_ERR(" len in sg %d %d %d\n", len, sg_ix, sg_cnt);
			return BC_STS_NOT_IMPL;
		}
		/* Setup DMA desc with Phy addr & Length at current index. */
		addr_temp.full_addr = crystalhd_get_sgle_paddr(ioreq, sg_ix);
		if (sg_ix == sg_st_ix) {
			addr_temp.full_addr += sg_st_off;
			len -= sg_st_off;
		}
		memset(&desc[ix], 0, sizeof(desc[ix]));
		desc[ix].buff_addr_low  = addr_temp.low_part;
		desc[ix].buff_addr_high = addr_temp.high_part;
		desc[ix].dma_dir        = ioreq->uinfo.dir_tx;

		/* Chain DMA descriptor.  */
		addr_temp.full_addr = desc_phy_addr + sizeof(struct dma_descriptor);
		desc[ix].next_desc_addr_low = addr_temp.low_part;
		desc[ix].next_desc_addr_high = addr_temp.high_part;

		if ((count + len) > xfr_sz)
			len = xfr_sz - count;

		/* Debug.. */
		if ((!len) || (len > crystalhd_get_sgle_len(ioreq, sg_ix))) {
			BCMLOG_ERR("inv-len(%x) Ix(%d) count:%x xfr_sz:%x sg_cnt:%d\n",
				   len, ix, count, xfr_sz, sg_cnt);
			return BC_STS_ERROR;
		}
		/* Length expects Multiple of 4 */
		desc[ix].xfer_size = (len / 4);

		crystalhd_hw_dump_desc(desc, ix, 1);

		count += len;
		desc_phy_addr += sizeof(struct dma_descriptor);
	}

	last_desc_ix = ix - 1;

	if (ioreq->fb_size) {
		memset(&desc[ix], 0, sizeof(desc[ix]));
		addr_temp.full_addr     = ioreq->fb_pa;
		desc[ix].buff_addr_low  = addr_temp.low_part;
		desc[ix].buff_addr_high = addr_temp.high_part;
		desc[ix].dma_dir        = ioreq->uinfo.dir_tx;
		desc[ix].xfer_size	= 1;
		desc[ix].fill_bytes	= 4 - ioreq->fb_size;
		count += ioreq->fb_size;
		last_desc_ix++;
	}

	/* setup last descriptor..*/
	desc[last_desc_ix].last_rec_indicator  = 1;
	desc[last_desc_ix].next_desc_addr_low  = 0;
	desc[last_desc_ix].next_desc_addr_high = 0;
	desc[last_desc_ix].intr_enable = 1;

	crystalhd_hw_dump_desc(desc, last_desc_ix, 1);

	if (count != xfr_sz) {
		BCMLOG_ERR("interal error sz curr:%x exp:%x\n", count, xfr_sz);
		return BC_STS_ERROR;
	}

	return BC_STS_SUCCESS;
}

static enum BC_STATUS crystalhd_xlat_sgl_to_dma_desc(struct crystalhd_dio_req *ioreq,
					      struct dma_desc_mem *pdesc_mem,
					      uint32_t *uv_desc_index)
{
	struct dma_descriptor *desc = NULL;
	dma_addr_t desc_paddr_base = 0;
	uint32_t sg_cnt = 0, sg_st_ix = 0, sg_st_off = 0;
	uint32_t xfr_sz = 0;
	enum BC_STATUS sts = BC_STS_SUCCESS;

	/* Check params.. */
	if (!ioreq || !pdesc_mem || !uv_desc_index) {
		BCMLOG_ERR("Invalid Args\n");
		return BC_STS_INV_ARG;
	}

	if (!pdesc_mem->sz || !pdesc_mem->pdma_desc_start ||
	    !ioreq->sg || (!ioreq->sg_cnt && !ioreq->uinfo.dir_tx)) {
		BCMLOG_ERR("Invalid Args\n");
		return BC_STS_INV_ARG;
	}

	if ((ioreq->uinfo.dir_tx) && (ioreq->uinfo.uv_offset)) {
		BCMLOG_ERR("UV offset for TX??\n");
		return BC_STS_INV_ARG;

	}

	desc = pdesc_mem->pdma_desc_start;
	desc_paddr_base = pdesc_mem->phy_addr;

	if (ioreq->uinfo.dir_tx || (ioreq->uinfo.uv_offset == 0)) {
		sg_cnt = ioreq->sg_cnt;
		xfr_sz = ioreq->uinfo.xfr_len;
	} else {
		sg_cnt = ioreq->uinfo.uv_sg_ix + 1;
		xfr_sz = ioreq->uinfo.uv_offset;
	}

	sts = crystalhd_hw_fill_desc(ioreq, desc, desc_paddr_base, sg_cnt,
				   sg_st_ix, sg_st_off, xfr_sz);

	if ((sts != BC_STS_SUCCESS) || !ioreq->uinfo.uv_offset)
		return sts;

	/* Prepare for UV mapping.. */
	desc = &pdesc_mem->pdma_desc_start[sg_cnt];
	desc_paddr_base = pdesc_mem->phy_addr +
			  (sg_cnt * sizeof(struct dma_descriptor));

	/* Done with desc addr.. now update sg stuff.*/
	sg_cnt    = ioreq->sg_cnt - ioreq->uinfo.uv_sg_ix;
	xfr_sz    = ioreq->uinfo.xfr_len - ioreq->uinfo.uv_offset;
	sg_st_ix  = ioreq->uinfo.uv_sg_ix;
	sg_st_off = ioreq->uinfo.uv_sg_off;

	sts = crystalhd_hw_fill_desc(ioreq, desc, desc_paddr_base, sg_cnt,
				   sg_st_ix, sg_st_off, xfr_sz);
	if (sts != BC_STS_SUCCESS)
		return sts;

	*uv_desc_index = sg_st_ix;

	return sts;
}

static void crystalhd_start_tx_dma_engine(struct crystalhd_hw *hw)
{
	uint32_t dma_cntrl;

	dma_cntrl = crystalhd_reg_rd(hw->adp, MISC1_TX_SW_DESC_LIST_CTRL_STS);
	if (!(dma_cntrl & DMA_START_BIT)) {
		dma_cntrl |= DMA_START_BIT;
		crystalhd_reg_wr(hw->adp, MISC1_TX_SW_DESC_LIST_CTRL_STS,
			       dma_cntrl);
	}

	return;
}

/* _CHECK_THIS_
 *
 * Verify if the Stop generates a completion interrupt or not.
 * if it does not generate an interrupt, then add polling here.
 */
static enum BC_STATUS crystalhd_stop_tx_dma_engine(struct crystalhd_hw *hw)
{
	uint32_t dma_cntrl, cnt = 30;
	uint32_t l1 = 1, l2 = 1;
	unsigned long flags = 0;

	dma_cntrl = crystalhd_reg_rd(hw->adp, MISC1_TX_SW_DESC_LIST_CTRL_STS);

	BCMLOG(BCMLOG_DBG, "Stopping TX DMA Engine..\n");

	/* FIXME: jarod: invert dma_ctrl and check bit? or are there missing parens? */
	if (!dma_cntrl & DMA_START_BIT) {
		BCMLOG(BCMLOG_DBG, "Already Stopped\n");
		return BC_STS_SUCCESS;
	}

	crystalhd_disable_interrupts(hw->adp);

	/* Issue stop to HW */
	/* This bit when set gave problems. Please check*/
	dma_cntrl &= ~DMA_START_BIT;
	crystalhd_reg_wr(hw->adp, MISC1_TX_SW_DESC_LIST_CTRL_STS, dma_cntrl);

	BCMLOG(BCMLOG_DBG, "Cleared the DMA Start bit\n");

	/* Poll for 3seconds (30 * 100ms) on both the lists..*/
	while ((l1 || l2) && cnt) {

		if (l1) {
			l1 = crystalhd_reg_rd(hw->adp, MISC1_TX_FIRST_DESC_L_ADDR_LIST0);
			l1 &= DMA_START_BIT;
		}

		if (l2) {
			l2 = crystalhd_reg_rd(hw->adp, MISC1_TX_FIRST_DESC_L_ADDR_LIST1);
			l2 &= DMA_START_BIT;
		}

		msleep_interruptible(100);

		cnt--;
	}

	if (!cnt) {
		BCMLOG_ERR("Failed to stop TX DMA.. l1 %d, l2 %d\n", l1, l2);
		crystalhd_enable_interrupts(hw->adp);
		return BC_STS_ERROR;
	}

	spin_lock_irqsave(&hw->lock, flags);
	hw->tx_list_post_index = 0;
	spin_unlock_irqrestore(&hw->lock, flags);
	BCMLOG(BCMLOG_DBG, "stopped TX DMA..\n");
	crystalhd_enable_interrupts(hw->adp);

	return BC_STS_SUCCESS;
}

static uint32_t crystalhd_get_pib_avail_cnt(struct crystalhd_hw *hw)
{
	/*
	* Position of the PIB Entries can be found at
	* 0th and the 1st location of the Circular list.
	*/
	uint32_t Q_addr;
	uint32_t pib_cnt, r_offset, w_offset;

	Q_addr = hw->pib_del_Q_addr;

	/* Get the Read Pointer */
	crystalhd_mem_rd(hw->adp, Q_addr, 1, &r_offset);

	/* Get the Write Pointer */
	crystalhd_mem_rd(hw->adp, Q_addr + sizeof(uint32_t), 1, &w_offset);

	if (r_offset == w_offset)
		return 0;	/* Queue is empty */

	if (w_offset > r_offset)
		pib_cnt = w_offset - r_offset;
	else
		pib_cnt = (w_offset + MAX_PIB_Q_DEPTH) -
			  (r_offset + MIN_PIB_Q_DEPTH);

	if (pib_cnt > MAX_PIB_Q_DEPTH) {
		BCMLOG_ERR("Invalid PIB Count (%u)\n", pib_cnt);
		return 0;
	}

	return pib_cnt;
}

static uint32_t crystalhd_get_addr_from_pib_Q(struct crystalhd_hw *hw)
{
	uint32_t Q_addr;
	uint32_t addr_entry, r_offset, w_offset;

	Q_addr = hw->pib_del_Q_addr;

	/* Get the Read Pointer 0Th Location is Read Pointer */
	crystalhd_mem_rd(hw->adp, Q_addr, 1, &r_offset);

	/* Get the Write Pointer 1st Location is Write pointer */
	crystalhd_mem_rd(hw->adp, Q_addr + sizeof(uint32_t), 1, &w_offset);

	/* Queue is empty */
	if (r_offset == w_offset)
		return 0;

	if ((r_offset < MIN_PIB_Q_DEPTH) || (r_offset >= MAX_PIB_Q_DEPTH))
		return 0;

	/* Get the Actual Address of the PIB */
	crystalhd_mem_rd(hw->adp, Q_addr + (r_offset * sizeof(uint32_t)),
		       1, &addr_entry);

	/* Increment the Read Pointer */
	r_offset++;

	if (MAX_PIB_Q_DEPTH == r_offset)
		r_offset = MIN_PIB_Q_DEPTH;

	/* Write back the read pointer to It's Location */
	crystalhd_mem_wr(hw->adp, Q_addr, 1, &r_offset);

	return addr_entry;
}

static bool crystalhd_rel_addr_to_pib_Q(struct crystalhd_hw *hw, uint32_t addr_to_rel)
{
	uint32_t Q_addr;
	uint32_t r_offset, w_offset, n_offset;

	Q_addr = hw->pib_rel_Q_addr;

	/* Get the Read Pointer */
	crystalhd_mem_rd(hw->adp, Q_addr, 1, &r_offset);

	/* Get the Write Pointer */
	crystalhd_mem_rd(hw->adp, Q_addr + sizeof(uint32_t), 1, &w_offset);

	if ((r_offset < MIN_PIB_Q_DEPTH) ||
	    (r_offset >= MAX_PIB_Q_DEPTH))
		return false;

	n_offset = w_offset + 1;

	if (MAX_PIB_Q_DEPTH == n_offset)
		n_offset = MIN_PIB_Q_DEPTH;

	if (r_offset == n_offset)
		return false; /* should never happen */

	/* Write the DRAM ADDR to the Queue at Next Offset */
	crystalhd_mem_wr(hw->adp, Q_addr + (w_offset * sizeof(uint32_t)),
		       1, &addr_to_rel);

	/* Put the New value of the write pointer in Queue */
	crystalhd_mem_wr(hw->adp, Q_addr + sizeof(uint32_t), 1, &n_offset);

	return true;
}

static void cpy_pib_to_app(struct c011_pib *src_pib, struct BC_PIC_INFO_BLOCK *dst_pib)
{
	if (!src_pib || !dst_pib) {
		BCMLOG_ERR("Invalid Arguments\n");
		return;
	}

	dst_pib->timeStamp           = 0;
	dst_pib->picture_number      = src_pib->ppb.picture_number;
	dst_pib->width               = src_pib->ppb.width;
	dst_pib->height              = src_pib->ppb.height;
	dst_pib->chroma_format       = src_pib->ppb.chroma_format;
	dst_pib->pulldown            = src_pib->ppb.pulldown;
	dst_pib->flags               = src_pib->ppb.flags;
	dst_pib->sess_num            = src_pib->ptsStcOffset;
	dst_pib->aspect_ratio        = src_pib->ppb.aspect_ratio;
	dst_pib->colour_primaries     = src_pib->ppb.colour_primaries;
	dst_pib->picture_meta_payload = src_pib->ppb.picture_meta_payload;
	dst_pib->frame_rate		= src_pib->resolution ;
	return;
}

static void crystalhd_hw_proc_pib(struct crystalhd_hw *hw)
{
	unsigned int cnt;
	struct c011_pib src_pib;
	uint32_t pib_addr, pib_cnt;
	struct BC_PIC_INFO_BLOCK *AppPib;
	struct crystalhd_rx_dma_pkt *rx_pkt = NULL;

	pib_cnt = crystalhd_get_pib_avail_cnt(hw);

	if (!pib_cnt)
		return;

	for (cnt = 0; cnt < pib_cnt; cnt++) {

		pib_addr = crystalhd_get_addr_from_pib_Q(hw);
		crystalhd_mem_rd(hw->adp, pib_addr, sizeof(struct c011_pib) / 4,
			       (uint32_t *)&src_pib);

		if (src_pib.bFormatChange) {
			rx_pkt = (struct crystalhd_rx_dma_pkt *)crystalhd_dioq_fetch(hw->rx_freeq);
			if (!rx_pkt)
				return;
			rx_pkt->flags = 0;
			rx_pkt->flags |= COMP_FLAG_PIB_VALID | COMP_FLAG_FMT_CHANGE;
			AppPib = &rx_pkt->pib;
			cpy_pib_to_app(&src_pib, AppPib);

			BCMLOG(BCMLOG_DBG,
			       "App PIB:%x %x %x %x %x %x %x %x %x %x\n",
			       rx_pkt->pib.picture_number,
			       rx_pkt->pib.aspect_ratio,
			       rx_pkt->pib.chroma_format,
			       rx_pkt->pib.colour_primaries,
			       rx_pkt->pib.frame_rate,
			       rx_pkt->pib.height,
			       rx_pkt->pib.height,
			       rx_pkt->pib.n_drop,
			       rx_pkt->pib.pulldown,
			       rx_pkt->pib.ycom);

			crystalhd_dioq_add(hw->rx_rdyq, (void *)rx_pkt, true, rx_pkt->pkt_tag);

		}

		crystalhd_rel_addr_to_pib_Q(hw, pib_addr);
	}
}

static void crystalhd_start_rx_dma_engine(struct crystalhd_hw *hw)
{
	uint32_t        dma_cntrl;

	dma_cntrl = crystalhd_reg_rd(hw->adp, MISC1_Y_RX_SW_DESC_LIST_CTRL_STS);
	if (!(dma_cntrl & DMA_START_BIT)) {
		dma_cntrl |= DMA_START_BIT;
		crystalhd_reg_wr(hw->adp, MISC1_Y_RX_SW_DESC_LIST_CTRL_STS, dma_cntrl);
	}

	dma_cntrl = crystalhd_reg_rd(hw->adp, MISC1_UV_RX_SW_DESC_LIST_CTRL_STS);
	if (!(dma_cntrl & DMA_START_BIT)) {
		dma_cntrl |= DMA_START_BIT;
		crystalhd_reg_wr(hw->adp, MISC1_UV_RX_SW_DESC_LIST_CTRL_STS, dma_cntrl);
	}

	return;
}

static void crystalhd_stop_rx_dma_engine(struct crystalhd_hw *hw)
{
	uint32_t dma_cntrl = 0, count = 30;
	uint32_t l0y = 1, l0uv = 1, l1y = 1, l1uv = 1;

	dma_cntrl = crystalhd_reg_rd(hw->adp, MISC1_Y_RX_SW_DESC_LIST_CTRL_STS);
	if ((dma_cntrl & DMA_START_BIT)) {
		dma_cntrl &= ~DMA_START_BIT;
		crystalhd_reg_wr(hw->adp, MISC1_Y_RX_SW_DESC_LIST_CTRL_STS, dma_cntrl);
	}

	dma_cntrl = crystalhd_reg_rd(hw->adp, MISC1_UV_RX_SW_DESC_LIST_CTRL_STS);
	if ((dma_cntrl & DMA_START_BIT)) {
		dma_cntrl &= ~DMA_START_BIT;
		crystalhd_reg_wr(hw->adp, MISC1_UV_RX_SW_DESC_LIST_CTRL_STS, dma_cntrl);
	}

	/* Poll for 3seconds (30 * 100ms) on both the lists..*/
	while ((l0y || l0uv || l1y || l1uv) && count) {

		if (l0y) {
			l0y = crystalhd_reg_rd(hw->adp, MISC1_Y_RX_FIRST_DESC_L_ADDR_LIST0);
			l0y &= DMA_START_BIT;
			if (!l0y)
				hw->rx_list_sts[0] &= ~rx_waiting_y_intr;
		}

		if (l1y) {
			l1y = crystalhd_reg_rd(hw->adp, MISC1_Y_RX_FIRST_DESC_L_ADDR_LIST1);
			l1y &= DMA_START_BIT;
			if (!l1y)
				hw->rx_list_sts[1] &= ~rx_waiting_y_intr;
		}

		if (l0uv) {
			l0uv = crystalhd_reg_rd(hw->adp, MISC1_UV_RX_FIRST_DESC_L_ADDR_LIST0);
			l0uv &= DMA_START_BIT;
			if (!l0uv)
				hw->rx_list_sts[0] &= ~rx_waiting_uv_intr;
		}

		if (l1uv) {
			l1uv = crystalhd_reg_rd(hw->adp, MISC1_UV_RX_FIRST_DESC_L_ADDR_LIST1);
			l1uv &= DMA_START_BIT;
			if (!l1uv)
				hw->rx_list_sts[1] &= ~rx_waiting_uv_intr;
		}
		msleep_interruptible(100);
		count--;
	}

	hw->rx_list_post_index = 0;

	BCMLOG(BCMLOG_SSTEP, "Capture Stop: %d List0:Sts:%x List1:Sts:%x\n",
	       count, hw->rx_list_sts[0], hw->rx_list_sts[1]);
}

static enum BC_STATUS crystalhd_hw_prog_rxdma(struct crystalhd_hw *hw, struct crystalhd_rx_dma_pkt *rx_pkt)
{
	uint32_t y_low_addr_reg, y_high_addr_reg;
	uint32_t uv_low_addr_reg, uv_high_addr_reg;
	union addr_64 desc_addr;
	unsigned long flags;

	if (!hw || !rx_pkt) {
		BCMLOG_ERR("Invalid Arguments\n");
		return BC_STS_INV_ARG;
	}

	if (hw->rx_list_post_index >= DMA_ENGINE_CNT) {
		BCMLOG_ERR("List Out Of bounds %x\n", hw->rx_list_post_index);
		return BC_STS_INV_ARG;
	}

	spin_lock_irqsave(&hw->rx_lock, flags);
	/* FIXME: jarod: sts_free is an enum for 0, in crystalhd_hw.h... yuk... */
	if (sts_free != hw->rx_list_sts[hw->rx_list_post_index]) {
		spin_unlock_irqrestore(&hw->rx_lock, flags);
		return BC_STS_BUSY;
	}

	if (!hw->rx_list_post_index) {
		y_low_addr_reg   = MISC1_Y_RX_FIRST_DESC_L_ADDR_LIST0;
		y_high_addr_reg  = MISC1_Y_RX_FIRST_DESC_U_ADDR_LIST0;
		uv_low_addr_reg  = MISC1_UV_RX_FIRST_DESC_L_ADDR_LIST0;
		uv_high_addr_reg = MISC1_UV_RX_FIRST_DESC_U_ADDR_LIST0;
	} else {
		y_low_addr_reg   = MISC1_Y_RX_FIRST_DESC_L_ADDR_LIST1;
		y_high_addr_reg  = MISC1_Y_RX_FIRST_DESC_U_ADDR_LIST1;
		uv_low_addr_reg  = MISC1_UV_RX_FIRST_DESC_L_ADDR_LIST1;
		uv_high_addr_reg = MISC1_UV_RX_FIRST_DESC_U_ADDR_LIST1;
	}
	rx_pkt->pkt_tag = hw->rx_pkt_tag_seed + hw->rx_list_post_index;
	hw->rx_list_sts[hw->rx_list_post_index] |= rx_waiting_y_intr;
	if (rx_pkt->uv_phy_addr)
		hw->rx_list_sts[hw->rx_list_post_index] |= rx_waiting_uv_intr;
	hw->rx_list_post_index = (hw->rx_list_post_index + 1) % DMA_ENGINE_CNT;
	spin_unlock_irqrestore(&hw->rx_lock, flags);

	crystalhd_dioq_add(hw->rx_actq, (void *)rx_pkt, false, rx_pkt->pkt_tag);

	crystalhd_start_rx_dma_engine(hw);
	/* Program the Y descriptor */
	desc_addr.full_addr = rx_pkt->desc_mem.phy_addr;
	crystalhd_reg_wr(hw->adp, y_high_addr_reg, desc_addr.high_part);
	crystalhd_reg_wr(hw->adp, y_low_addr_reg, desc_addr.low_part | 0x01);

	if (rx_pkt->uv_phy_addr) {
		/* Program the UV descriptor */
		desc_addr.full_addr = rx_pkt->uv_phy_addr;
		crystalhd_reg_wr(hw->adp, uv_high_addr_reg, desc_addr.high_part);
		crystalhd_reg_wr(hw->adp, uv_low_addr_reg, desc_addr.low_part | 0x01);
	}

	return BC_STS_SUCCESS;
}

static enum BC_STATUS crystalhd_hw_post_cap_buff(struct crystalhd_hw *hw,
					  struct crystalhd_rx_dma_pkt *rx_pkt)
{
	enum BC_STATUS sts = crystalhd_hw_prog_rxdma(hw, rx_pkt);

	if (sts == BC_STS_BUSY)
		crystalhd_dioq_add(hw->rx_freeq, (void *)rx_pkt,
				 false, rx_pkt->pkt_tag);

	return sts;
}

static void crystalhd_get_dnsz(struct crystalhd_hw *hw, uint32_t list_index,
			     uint32_t *y_dw_dnsz, uint32_t *uv_dw_dnsz)
{
	uint32_t y_dn_sz_reg, uv_dn_sz_reg;

	if (!list_index) {
		y_dn_sz_reg  = MISC1_Y_RX_LIST0_CUR_BYTE_CNT;
		uv_dn_sz_reg = MISC1_UV_RX_LIST0_CUR_BYTE_CNT;
	} else {
		y_dn_sz_reg  = MISC1_Y_RX_LIST1_CUR_BYTE_CNT;
		uv_dn_sz_reg = MISC1_UV_RX_LIST1_CUR_BYTE_CNT;
	}

	*y_dw_dnsz  = crystalhd_reg_rd(hw->adp, y_dn_sz_reg);
	*uv_dw_dnsz = crystalhd_reg_rd(hw->adp, uv_dn_sz_reg);
}

/*
 * This function should be called only after making sure that the two DMA
 * lists are free. This function does not check if DMA's are active, before
 * turning off the DMA.
 */
static void crystalhd_hw_finalize_pause(struct crystalhd_hw *hw)
{
	uint32_t dma_cntrl, aspm;

	hw->stop_pending = 0;

	dma_cntrl = crystalhd_reg_rd(hw->adp, MISC1_Y_RX_SW_DESC_LIST_CTRL_STS);
	if (dma_cntrl & DMA_START_BIT) {
		dma_cntrl &= ~DMA_START_BIT;
		crystalhd_reg_wr(hw->adp, MISC1_Y_RX_SW_DESC_LIST_CTRL_STS, dma_cntrl);
	}

	dma_cntrl = crystalhd_reg_rd(hw->adp, MISC1_UV_RX_SW_DESC_LIST_CTRL_STS);
	if (dma_cntrl & DMA_START_BIT) {
		dma_cntrl &= ~DMA_START_BIT;
		crystalhd_reg_wr(hw->adp, MISC1_UV_RX_SW_DESC_LIST_CTRL_STS, dma_cntrl);
	}
	hw->rx_list_post_index = 0;

	aspm = crystalhd_reg_rd(hw->adp, PCIE_DLL_DATA_LINK_CONTROL);
	aspm |= ASPM_L1_ENABLE;
	/* NAREN BCMLOG(BCMLOG_INFO, "aspm on\n"); */
	crystalhd_reg_wr(hw->adp, PCIE_DLL_DATA_LINK_CONTROL, aspm);
}

static enum BC_STATUS crystalhd_rx_pkt_done(struct crystalhd_hw *hw, uint32_t list_index,
				     enum BC_STATUS comp_sts)
{
	struct crystalhd_rx_dma_pkt *rx_pkt = NULL;
	uint32_t y_dw_dnsz, uv_dw_dnsz;
	enum BC_STATUS sts = BC_STS_SUCCESS;

	if (!hw || list_index >= DMA_ENGINE_CNT) {
		BCMLOG_ERR("Invalid Arguments\n");
		return BC_STS_INV_ARG;
	}

	rx_pkt = crystalhd_dioq_find_and_fetch(hw->rx_actq,
					     hw->rx_pkt_tag_seed + list_index);
	if (!rx_pkt) {
		BCMLOG_ERR("Act-Q:PostIx:%x L0Sts:%x L1Sts:%x current L:%x tag:%x comp:%x\n",
			   hw->rx_list_post_index, hw->rx_list_sts[0],
			   hw->rx_list_sts[1], list_index,
			   hw->rx_pkt_tag_seed + list_index, comp_sts);
		return BC_STS_INV_ARG;
	}

	if (comp_sts == BC_STS_SUCCESS) {
		crystalhd_get_dnsz(hw, list_index, &y_dw_dnsz, &uv_dw_dnsz);
		rx_pkt->dio_req->uinfo.y_done_sz = y_dw_dnsz;
		rx_pkt->flags = COMP_FLAG_DATA_VALID;
		if (rx_pkt->uv_phy_addr)
			rx_pkt->dio_req->uinfo.uv_done_sz = uv_dw_dnsz;
		crystalhd_dioq_add(hw->rx_rdyq, rx_pkt, true,
				hw->rx_pkt_tag_seed + list_index);
		return sts;
	}

	/* Check if we can post this DIO again. */
	return crystalhd_hw_post_cap_buff(hw, rx_pkt);
}

static bool crystalhd_rx_list0_handler(struct crystalhd_hw *hw, uint32_t int_sts,
				     uint32_t y_err_sts, uint32_t uv_err_sts)
{
	uint32_t tmp;
	enum list_sts tmp_lsts;

	if (!(y_err_sts & GET_Y0_ERR_MSK) && !(uv_err_sts & GET_UV0_ERR_MSK))
		return false;

	tmp_lsts = hw->rx_list_sts[0];

	/* Y0 - DMA */
	tmp = y_err_sts & GET_Y0_ERR_MSK;
	if (int_sts & INTR_INTR_STATUS_L0_Y_RX_DMA_DONE_INTR_MASK)
		hw->rx_list_sts[0] &= ~rx_waiting_y_intr;

	if (y_err_sts & MISC1_Y_RX_ERROR_STATUS_RX_L0_UNDERRUN_ERROR_MASK) {
		hw->rx_list_sts[0] &= ~rx_waiting_y_intr;
		tmp &= ~MISC1_Y_RX_ERROR_STATUS_RX_L0_UNDERRUN_ERROR_MASK;
	}

	if (y_err_sts & MISC1_Y_RX_ERROR_STATUS_RX_L0_FIFO_FULL_ERRORS_MASK) {
		hw->rx_list_sts[0] &= ~rx_y_mask;
		hw->rx_list_sts[0] |= rx_y_error;
		tmp &= ~MISC1_Y_RX_ERROR_STATUS_RX_L0_FIFO_FULL_ERRORS_MASK;
	}

	if (tmp) {
		hw->rx_list_sts[0] &= ~rx_y_mask;
		hw->rx_list_sts[0] |= rx_y_error;
		hw->rx_list_post_index = 0;
	}

	/* UV0 - DMA */
	tmp = uv_err_sts & GET_UV0_ERR_MSK;
	if (int_sts & INTR_INTR_STATUS_L0_UV_RX_DMA_DONE_INTR_MASK)
		hw->rx_list_sts[0] &= ~rx_waiting_uv_intr;

	if (uv_err_sts & MISC1_UV_RX_ERROR_STATUS_RX_L0_UNDERRUN_ERROR_MASK) {
		hw->rx_list_sts[0] &= ~rx_waiting_uv_intr;
		tmp &= ~MISC1_UV_RX_ERROR_STATUS_RX_L0_UNDERRUN_ERROR_MASK;
	}

	if (uv_err_sts & MISC1_UV_RX_ERROR_STATUS_RX_L0_FIFO_FULL_ERRORS_MASK) {
		hw->rx_list_sts[0] &= ~rx_uv_mask;
		hw->rx_list_sts[0] |= rx_uv_error;
		tmp &= ~MISC1_UV_RX_ERROR_STATUS_RX_L0_FIFO_FULL_ERRORS_MASK;
	}

	if (tmp) {
		hw->rx_list_sts[0] &= ~rx_uv_mask;
		hw->rx_list_sts[0] |= rx_uv_error;
		hw->rx_list_post_index = 0;
	}

	if (y_err_sts & GET_Y0_ERR_MSK) {
		tmp = y_err_sts & GET_Y0_ERR_MSK;
		crystalhd_reg_wr(hw->adp, MISC1_Y_RX_ERROR_STATUS, tmp);
	}

	if (uv_err_sts & GET_UV0_ERR_MSK) {
		tmp = uv_err_sts & GET_UV0_ERR_MSK;
		crystalhd_reg_wr(hw->adp, MISC1_UV_RX_ERROR_STATUS, tmp);
	}

	return (tmp_lsts != hw->rx_list_sts[0]);
}

static bool crystalhd_rx_list1_handler(struct crystalhd_hw *hw, uint32_t int_sts,
				     uint32_t y_err_sts, uint32_t uv_err_sts)
{
	uint32_t tmp;
	enum list_sts tmp_lsts;

	if (!(y_err_sts & GET_Y1_ERR_MSK) && !(uv_err_sts & GET_UV1_ERR_MSK))
		return false;

	tmp_lsts = hw->rx_list_sts[1];

	/* Y1 - DMA */
	tmp = y_err_sts & GET_Y1_ERR_MSK;
	if (int_sts & INTR_INTR_STATUS_L1_Y_RX_DMA_DONE_INTR_MASK)
		hw->rx_list_sts[1] &= ~rx_waiting_y_intr;

	if (y_err_sts & MISC1_Y_RX_ERROR_STATUS_RX_L1_UNDERRUN_ERROR_MASK) {
		hw->rx_list_sts[1] &= ~rx_waiting_y_intr;
		tmp &= ~MISC1_Y_RX_ERROR_STATUS_RX_L1_UNDERRUN_ERROR_MASK;
	}

	if (y_err_sts & MISC1_Y_RX_ERROR_STATUS_RX_L1_FIFO_FULL_ERRORS_MASK) {
		/* Add retry-support..*/
		hw->rx_list_sts[1] &= ~rx_y_mask;
		hw->rx_list_sts[1] |= rx_y_error;
		tmp &= ~MISC1_Y_RX_ERROR_STATUS_RX_L1_FIFO_FULL_ERRORS_MASK;
	}

	if (tmp) {
		hw->rx_list_sts[1] &= ~rx_y_mask;
		hw->rx_list_sts[1] |= rx_y_error;
		hw->rx_list_post_index = 0;
	}

	/* UV1 - DMA */
	tmp = uv_err_sts & GET_UV1_ERR_MSK;
	if (int_sts & INTR_INTR_STATUS_L1_UV_RX_DMA_DONE_INTR_MASK)
		hw->rx_list_sts[1] &= ~rx_waiting_uv_intr;

	if (uv_err_sts & MISC1_UV_RX_ERROR_STATUS_RX_L1_UNDERRUN_ERROR_MASK) {
		hw->rx_list_sts[1] &= ~rx_waiting_uv_intr;
		tmp &= ~MISC1_UV_RX_ERROR_STATUS_RX_L1_UNDERRUN_ERROR_MASK;
	}

	if (uv_err_sts & MISC1_UV_RX_ERROR_STATUS_RX_L1_FIFO_FULL_ERRORS_MASK) {
		/* Add retry-support*/
		hw->rx_list_sts[1] &= ~rx_uv_mask;
		hw->rx_list_sts[1] |= rx_uv_error;
		tmp &= ~MISC1_UV_RX_ERROR_STATUS_RX_L1_FIFO_FULL_ERRORS_MASK;
	}

	if (tmp) {
		hw->rx_list_sts[1] &= ~rx_uv_mask;
		hw->rx_list_sts[1] |= rx_uv_error;
		hw->rx_list_post_index = 0;
	}

	if (y_err_sts & GET_Y1_ERR_MSK) {
		tmp = y_err_sts & GET_Y1_ERR_MSK;
		crystalhd_reg_wr(hw->adp, MISC1_Y_RX_ERROR_STATUS, tmp);
	}

	if (uv_err_sts & GET_UV1_ERR_MSK) {
		tmp = uv_err_sts & GET_UV1_ERR_MSK;
		crystalhd_reg_wr(hw->adp, MISC1_UV_RX_ERROR_STATUS, tmp);
	}

	return (tmp_lsts != hw->rx_list_sts[1]);
}


static void crystalhd_rx_isr(struct crystalhd_hw *hw, uint32_t intr_sts)
{
	unsigned long flags;
	uint32_t i, list_avail = 0;
	enum BC_STATUS comp_sts = BC_STS_NO_DATA;
	uint32_t y_err_sts, uv_err_sts, y_dn_sz = 0, uv_dn_sz = 0;
	bool ret = 0;

	if (!hw) {
		BCMLOG_ERR("Invalid Arguments\n");
		return;
	}

	if (!(intr_sts & GET_RX_INTR_MASK))
		return;

	y_err_sts = crystalhd_reg_rd(hw->adp, MISC1_Y_RX_ERROR_STATUS);
	uv_err_sts = crystalhd_reg_rd(hw->adp, MISC1_UV_RX_ERROR_STATUS);

	for (i = 0; i < DMA_ENGINE_CNT; i++) {
		/* Update States..*/
		spin_lock_irqsave(&hw->rx_lock, flags);
		if (i == 0)
			ret = crystalhd_rx_list0_handler(hw, intr_sts, y_err_sts, uv_err_sts);
		else
			ret = crystalhd_rx_list1_handler(hw, intr_sts, y_err_sts, uv_err_sts);
		if (ret) {
			switch (hw->rx_list_sts[i]) {
			case sts_free:
				comp_sts = BC_STS_SUCCESS;
				list_avail = 1;
				break;
			case rx_y_error:
			case rx_uv_error:
			case rx_sts_error:
				/* We got error on both or Y or uv. */
				hw->stats.rx_errors++;
				crystalhd_get_dnsz(hw, i, &y_dn_sz, &uv_dn_sz);
				/* FIXME: jarod: this is where my mini pci-e card is tripping up */
				BCMLOG(BCMLOG_DBG, "list_index:%x rx[%d] Y:%x "
				       "UV:%x Int:%x YDnSz:%x UVDnSz:%x\n",
				       i, hw->stats.rx_errors, y_err_sts,
				       uv_err_sts, intr_sts, y_dn_sz, uv_dn_sz);
				hw->rx_list_sts[i] = sts_free;
				comp_sts = BC_STS_ERROR;
				break;
			default:
				/* Wait for completion..*/
				comp_sts = BC_STS_NO_DATA;
				break;
			}
		}
		spin_unlock_irqrestore(&hw->rx_lock, flags);

		/* handle completion...*/
		if (comp_sts != BC_STS_NO_DATA) {
			crystalhd_rx_pkt_done(hw, i, comp_sts);
			comp_sts = BC_STS_NO_DATA;
		}
	}

	if (list_avail) {
		if (hw->stop_pending) {
			if ((hw->rx_list_sts[0] == sts_free) &&
			    (hw->rx_list_sts[1] == sts_free))
				crystalhd_hw_finalize_pause(hw);
		} else {
			crystalhd_hw_start_capture(hw);
		}
	}
}

static enum BC_STATUS crystalhd_fw_cmd_post_proc(struct crystalhd_hw *hw,
					  struct BC_FW_CMD *fw_cmd)
{
	enum BC_STATUS sts = BC_STS_SUCCESS;
	struct dec_rsp_channel_start_video *st_rsp = NULL;

	switch (fw_cmd->cmd[0]) {
	case eCMD_C011_DEC_CHAN_START_VIDEO:
		st_rsp = (struct dec_rsp_channel_start_video *)fw_cmd->rsp;
		hw->pib_del_Q_addr = st_rsp->picInfoDeliveryQ;
		hw->pib_rel_Q_addr = st_rsp->picInfoReleaseQ;
		BCMLOG(BCMLOG_DBG, "DelQAddr:%x RelQAddr:%x\n",
		       hw->pib_del_Q_addr, hw->pib_rel_Q_addr);
		break;
	case eCMD_C011_INIT:
		if (!(crystalhd_load_firmware_config(hw->adp))) {
			BCMLOG_ERR("Invalid Params.\n");
			sts = BC_STS_FW_AUTH_FAILED;
		}
		break;
	default:
		break;
	}
	return sts;
}

static enum BC_STATUS crystalhd_put_ddr2sleep(struct crystalhd_hw *hw)
{
	uint32_t reg;
	union link_misc_perst_decoder_ctrl rst_cntrl_reg;

	/* Pulse reset pin of 7412 (MISC_PERST_DECODER_CTRL) */
	rst_cntrl_reg.whole_reg = crystalhd_reg_rd(hw->adp, MISC_PERST_DECODER_CTRL);

	rst_cntrl_reg.bcm_7412_rst = 1;
	crystalhd_reg_wr(hw->adp, MISC_PERST_DECODER_CTRL, rst_cntrl_reg.whole_reg);
	msleep_interruptible(50);

	rst_cntrl_reg.bcm_7412_rst = 0;
	crystalhd_reg_wr(hw->adp, MISC_PERST_DECODER_CTRL, rst_cntrl_reg.whole_reg);

	/* Close all banks, put DDR in idle */
	bc_dec_reg_wr(hw->adp, SDRAM_PRECHARGE, 0);

	/* Set bit 25 (drop CKE pin of DDR) */
	reg = bc_dec_reg_rd(hw->adp, SDRAM_PARAM);
	reg |= 0x02000000;
	bc_dec_reg_wr(hw->adp, SDRAM_PARAM, reg);

	/* Reset the audio block */
	bc_dec_reg_wr(hw->adp, AUD_DSP_MISC_SOFT_RESET, 0x1);

	/* Power down Raptor PLL */
	reg = bc_dec_reg_rd(hw->adp, DecHt_PllCCtl);
	reg |= 0x00008000;
	bc_dec_reg_wr(hw->adp, DecHt_PllCCtl, reg);

	/* Power down all Audio PLL */
	bc_dec_reg_wr(hw->adp, AIO_MISC_PLL_RESET, 0x1);

	/* Power down video clock (75MHz) */
	reg = bc_dec_reg_rd(hw->adp, DecHt_PllECtl);
	reg |= 0x00008000;
	bc_dec_reg_wr(hw->adp, DecHt_PllECtl, reg);

	/* Power down video clock (75MHz) */
	reg = bc_dec_reg_rd(hw->adp, DecHt_PllDCtl);
	reg |= 0x00008000;
	bc_dec_reg_wr(hw->adp, DecHt_PllDCtl, reg);

	/* Power down core clock (200MHz) */
	reg = bc_dec_reg_rd(hw->adp, DecHt_PllACtl);
	reg |= 0x00008000;
	bc_dec_reg_wr(hw->adp, DecHt_PllACtl, reg);

	/* Power down core clock (200MHz) */
	reg = bc_dec_reg_rd(hw->adp, DecHt_PllBCtl);
	reg |= 0x00008000;
	bc_dec_reg_wr(hw->adp, DecHt_PllBCtl, reg);

	return BC_STS_SUCCESS;
}

/************************************************
**
*************************************************/

enum BC_STATUS crystalhd_download_fw(struct crystalhd_adp *adp, void *buffer, uint32_t sz)
{
	uint32_t reg_data, cnt, *temp_buff;
	uint32_t fw_sig_len = 36;
	uint32_t dram_offset = BC_FWIMG_ST_ADDR, sig_reg;

	BCMLOG_ENTER;

	if (!adp || !buffer || !sz) {
		BCMLOG_ERR("Invalid Params.\n");
		return BC_STS_INV_ARG;
	}

	reg_data = crystalhd_reg_rd(adp, OTP_CMD);
	if (!(reg_data & 0x02)) {
		BCMLOG_ERR("Invalid hw config.. otp not programmed\n");
		return BC_STS_ERROR;
	}

	reg_data = 0;
	crystalhd_reg_wr(adp, DCI_CMD, 0);
	reg_data |= BC_BIT(0);
	crystalhd_reg_wr(adp, DCI_CMD, reg_data);

	reg_data = 0;
	cnt = 1000;
	msleep_interruptible(10);

	while (reg_data != BC_BIT(4)) {
		reg_data = crystalhd_reg_rd(adp, DCI_STATUS);
		reg_data &= BC_BIT(4);
		if (--cnt == 0) {
			BCMLOG_ERR("Firmware Download RDY Timeout.\n");
			return BC_STS_TIMEOUT;
		}
	}

	msleep_interruptible(10);
	/*  Load the FW to the FW_ADDR field in the DCI_FIRMWARE_ADDR */
	crystalhd_reg_wr(adp, DCI_FIRMWARE_ADDR, dram_offset);
	temp_buff = (uint32_t *)buffer;
	for (cnt = 0; cnt < (sz - fw_sig_len); cnt += 4) {
		crystalhd_reg_wr(adp, DCI_DRAM_BASE_ADDR, (dram_offset >> 19));
		crystalhd_reg_wr(adp, DCI_FIRMWARE_DATA, *temp_buff);
		dram_offset += 4;
		temp_buff++;
	}
	msleep_interruptible(10);

	temp_buff++;

	sig_reg = (uint32_t)DCI_SIGNATURE_DATA_7;
	for (cnt = 0; cnt < 8; cnt++) {
		uint32_t swapped_data = *temp_buff;
		swapped_data = bswap_32_1(swapped_data);
		crystalhd_reg_wr(adp, sig_reg, swapped_data);
		sig_reg -= 4;
		temp_buff++;
	}
	msleep_interruptible(10);

	reg_data = 0;
	reg_data |= BC_BIT(1);
	crystalhd_reg_wr(adp, DCI_CMD, reg_data);
	msleep_interruptible(10);

	reg_data = 0;
	reg_data = crystalhd_reg_rd(adp, DCI_STATUS);

	if ((reg_data & BC_BIT(9)) == BC_BIT(9)) {
		cnt = 1000;
		while ((reg_data & BC_BIT(0)) != BC_BIT(0)) {
			reg_data = crystalhd_reg_rd(adp, DCI_STATUS);
			reg_data &= BC_BIT(0);
			if (!(--cnt))
				break;
			msleep_interruptible(10);
		}
		reg_data = 0;
		reg_data = crystalhd_reg_rd(adp, DCI_CMD);
		reg_data |= BC_BIT(4);
		crystalhd_reg_wr(adp, DCI_CMD, reg_data);

	} else {
		BCMLOG_ERR("F/w Signature mismatch\n");
		return BC_STS_FW_AUTH_FAILED;
	}

	BCMLOG(BCMLOG_INFO, "Firmware Downloaded Successfully\n");
	return BC_STS_SUCCESS;
}

enum BC_STATUS crystalhd_do_fw_cmd(struct crystalhd_hw *hw,
				struct BC_FW_CMD *fw_cmd)
{
	uint32_t cnt = 0, cmd_res_addr;
	uint32_t *cmd_buff, *res_buff;
	wait_queue_head_t fw_cmd_event;
	int rc = 0;
	enum BC_STATUS sts;

	crystalhd_create_event(&fw_cmd_event);

	BCMLOG_ENTER;

	if (!hw || !fw_cmd) {
		BCMLOG_ERR("Invalid Arguments\n");
		return BC_STS_INV_ARG;
	}

	cmd_buff = fw_cmd->cmd;
	res_buff = fw_cmd->rsp;

	if (!cmd_buff || !res_buff) {
		BCMLOG_ERR("Invalid Parameters for F/W Command\n");
		return BC_STS_INV_ARG;
	}

	hw->pwr_lock++;

	hw->fwcmd_evt_sts = 0;
	hw->pfw_cmd_event = &fw_cmd_event;

	/*Write the command to the memory*/
	crystalhd_mem_wr(hw->adp, TS_Host2CpuSnd, FW_CMD_BUFF_SZ, cmd_buff);

	/*Memory Read for memory arbitrator flush*/
	crystalhd_mem_rd(hw->adp, TS_Host2CpuSnd, 1, &cnt);

	/* Write the command address to mailbox */
	bc_dec_reg_wr(hw->adp, Hst2CpuMbx1, TS_Host2CpuSnd);
	msleep_interruptible(50);

	crystalhd_wait_on_event(&fw_cmd_event, hw->fwcmd_evt_sts, 20000, rc, 0);

	if (!rc) {
		sts = BC_STS_SUCCESS;
	} else if (rc == -EBUSY) {
		BCMLOG_ERR("Firmware command T/O\n");
		sts = BC_STS_TIMEOUT;
	} else if (rc == -EINTR) {
		BCMLOG(BCMLOG_DBG, "FwCmd Wait Signal int.\n");
		sts = BC_STS_IO_USER_ABORT;
	} else {
		BCMLOG_ERR("FwCmd IO Error.\n");
		sts = BC_STS_IO_ERROR;
	}

	if (sts != BC_STS_SUCCESS) {
		BCMLOG_ERR("FwCmd Failed.\n");
		hw->pwr_lock--;
		return sts;
	}

	/*Get the Responce Address*/
	cmd_res_addr = bc_dec_reg_rd(hw->adp, Cpu2HstMbx1);

	/*Read the Response*/
	crystalhd_mem_rd(hw->adp, cmd_res_addr, FW_CMD_BUFF_SZ, res_buff);

	hw->pwr_lock--;

	if (res_buff[2] != C011_RET_SUCCESS) {
		BCMLOG_ERR("res_buff[2] != C011_RET_SUCCESS\n");
		return BC_STS_FW_CMD_ERR;
	}

	sts = crystalhd_fw_cmd_post_proc(hw, fw_cmd);
	if (sts != BC_STS_SUCCESS)
		BCMLOG_ERR("crystalhd_fw_cmd_post_proc Failed.\n");

	return sts;
}

bool crystalhd_hw_interrupt(struct crystalhd_adp *adp, struct crystalhd_hw *hw)
{
	uint32_t intr_sts = 0;
	uint32_t deco_intr = 0;
	bool rc = 0;

	if (!adp || !hw->dev_started)
		return rc;

	hw->stats.num_interrupts++;
	hw->pwr_lock++;

	deco_intr = bc_dec_reg_rd(adp, Stream2Host_Intr_Sts);
	intr_sts  = crystalhd_reg_rd(adp, INTR_INTR_STATUS);

	if (intr_sts) {
		/* let system know we processed interrupt..*/
		rc = 1;
		hw->stats.dev_interrupts++;
	}

	if (deco_intr && (deco_intr != 0xdeaddead)) {

		if (deco_intr & 0x80000000) {
			/*Set the Event and the status flag*/
			if (hw->pfw_cmd_event) {
				hw->fwcmd_evt_sts = 1;
				crystalhd_set_event(hw->pfw_cmd_event);
			}
		}

		if (deco_intr & BC_BIT(1))
			crystalhd_hw_proc_pib(hw);

		bc_dec_reg_wr(adp, Stream2Host_Intr_Sts, deco_intr);
		/* FIXME: jarod: No udelay? might this be the real reason mini pci-e cards were stalling out? */
		bc_dec_reg_wr(adp, Stream2Host_Intr_Sts, 0);
		rc = 1;
	}

	/* Rx interrupts */
	crystalhd_rx_isr(hw, intr_sts);

	/* Tx interrupts*/
	crystalhd_tx_isr(hw, intr_sts);

	/* Clear interrupts */
	if (rc) {
		if (intr_sts)
			crystalhd_reg_wr(adp, INTR_INTR_CLR_REG, intr_sts);

		crystalhd_reg_wr(adp, INTR_EOI_CTRL, 1);
	}

	hw->pwr_lock--;

	return rc;
}

enum BC_STATUS crystalhd_hw_open(struct crystalhd_hw *hw, struct crystalhd_adp *adp)
{
	if (!hw || !adp) {
		BCMLOG_ERR("Invalid Arguments\n");
		return BC_STS_INV_ARG;
	}

	if (hw->dev_started)
		return BC_STS_SUCCESS;

	memset(hw, 0, sizeof(struct crystalhd_hw));

	hw->adp = adp;
	spin_lock_init(&hw->lock);
	spin_lock_init(&hw->rx_lock);
	/* FIXME: jarod: what are these magic numbers?!? */
	hw->tx_ioq_tag_seed = 0x70023070;
	hw->rx_pkt_tag_seed = 0x70029070;

	hw->stop_pending = 0;
	crystalhd_start_device(hw->adp);
	hw->dev_started = true;

	/* set initial core clock  */
	hw->core_clock_mhz = CLOCK_PRESET;
	hw->prev_n = 0;
	hw->pwr_lock = 0;
	crystalhd_hw_set_core_clock(hw);

	return BC_STS_SUCCESS;
}

enum BC_STATUS crystalhd_hw_close(struct crystalhd_hw *hw)
{
	if (!hw) {
		BCMLOG_ERR("Invalid Arguments\n");
		return BC_STS_INV_ARG;
	}

	if (!hw->dev_started)
		return BC_STS_SUCCESS;

	/* Stop and DDR sleep will happen in here */
	crystalhd_hw_suspend(hw);
	hw->dev_started = false;

	return BC_STS_SUCCESS;
}

enum BC_STATUS crystalhd_hw_setup_dma_rings(struct crystalhd_hw *hw)
{
	unsigned int i;
	void *mem;
	size_t mem_len;
	dma_addr_t phy_addr;
	enum BC_STATUS sts = BC_STS_SUCCESS;
	struct crystalhd_rx_dma_pkt *rpkt;

	if (!hw || !hw->adp) {
		BCMLOG_ERR("Invalid Arguments\n");
		return BC_STS_INV_ARG;
	}

	sts = crystalhd_hw_create_ioqs(hw);
	if (sts != BC_STS_SUCCESS) {
		BCMLOG_ERR("Failed to create IOQs..\n");
		return sts;
	}

	mem_len = BC_LINK_MAX_SGLS * sizeof(struct dma_descriptor);

	for (i = 0; i < BC_TX_LIST_CNT; i++) {
		mem = bc_kern_dma_alloc(hw->adp, mem_len, &phy_addr);
		if (mem) {
			memset(mem, 0, mem_len);
		} else {
			BCMLOG_ERR("Insufficient Memory For TX\n");
			crystalhd_hw_free_dma_rings(hw);
			return BC_STS_INSUFF_RES;
		}
		/* rx_pkt_pool -- static memory allocation  */
		hw->tx_pkt_pool[i].desc_mem.pdma_desc_start = mem;
		hw->tx_pkt_pool[i].desc_mem.phy_addr = phy_addr;
		hw->tx_pkt_pool[i].desc_mem.sz = BC_LINK_MAX_SGLS *
						 sizeof(struct dma_descriptor);
		hw->tx_pkt_pool[i].list_tag = 0;

		/* Add TX dma requests to Free Queue..*/
		sts = crystalhd_dioq_add(hw->tx_freeq,
				       &hw->tx_pkt_pool[i], false, 0);
		if (sts != BC_STS_SUCCESS) {
			crystalhd_hw_free_dma_rings(hw);
			return sts;
		}
	}

	for (i = 0; i < BC_RX_LIST_CNT; i++) {
		rpkt = kzalloc(sizeof(*rpkt), GFP_KERNEL);
		if (!rpkt) {
			BCMLOG_ERR("Insufficient Memory For RX\n");
			crystalhd_hw_free_dma_rings(hw);
			return BC_STS_INSUFF_RES;
		}

		mem = bc_kern_dma_alloc(hw->adp, mem_len, &phy_addr);
		if (mem) {
			memset(mem, 0, mem_len);
		} else {
			BCMLOG_ERR("Insufficient Memory For RX\n");
			crystalhd_hw_free_dma_rings(hw);
			return BC_STS_INSUFF_RES;
		}
		rpkt->desc_mem.pdma_desc_start = mem;
		rpkt->desc_mem.phy_addr = phy_addr;
		rpkt->desc_mem.sz  = BC_LINK_MAX_SGLS * sizeof(struct dma_descriptor);
		rpkt->pkt_tag = hw->rx_pkt_tag_seed + i;
		crystalhd_hw_free_rx_pkt(hw, rpkt);
	}

	return BC_STS_SUCCESS;
}

enum BC_STATUS crystalhd_hw_free_dma_rings(struct crystalhd_hw *hw)
{
	unsigned int i;
	struct crystalhd_rx_dma_pkt *rpkt = NULL;

	if (!hw || !hw->adp) {
		BCMLOG_ERR("Invalid Arguments\n");
		return BC_STS_INV_ARG;
	}

	/* Delete all IOQs.. */
	crystalhd_hw_delete_ioqs(hw);

	for (i = 0; i < BC_TX_LIST_CNT; i++) {
		if (hw->tx_pkt_pool[i].desc_mem.pdma_desc_start) {
			bc_kern_dma_free(hw->adp,
				hw->tx_pkt_pool[i].desc_mem.sz,
				hw->tx_pkt_pool[i].desc_mem.pdma_desc_start,
				hw->tx_pkt_pool[i].desc_mem.phy_addr);

			hw->tx_pkt_pool[i].desc_mem.pdma_desc_start = NULL;
		}
	}

	BCMLOG(BCMLOG_DBG, "Releasing RX Pkt pool\n");
	do {
		rpkt = crystalhd_hw_alloc_rx_pkt(hw);
		if (!rpkt)
			break;
		bc_kern_dma_free(hw->adp, rpkt->desc_mem.sz,
				 rpkt->desc_mem.pdma_desc_start,
				 rpkt->desc_mem.phy_addr);
		kfree(rpkt);
	} while (rpkt);

	return BC_STS_SUCCESS;
}

enum BC_STATUS crystalhd_hw_post_tx(struct crystalhd_hw *hw, struct crystalhd_dio_req *ioreq,
			     hw_comp_callback call_back,
			     wait_queue_head_t *cb_event, uint32_t *list_id,
			     uint8_t data_flags)
{
	struct tx_dma_pkt *tx_dma_packet = NULL;
	uint32_t first_desc_u_addr, first_desc_l_addr;
	uint32_t low_addr, high_addr;
	union addr_64 desc_addr;
	enum BC_STATUS sts, add_sts;
	uint32_t dummy_index = 0;
	unsigned long flags;
	bool rc;

	if (!hw || !ioreq || !call_back || !cb_event || !list_id) {
		BCMLOG_ERR("Invalid Arguments\n");
		return BC_STS_INV_ARG;
	}

	/*
	 * Since we hit code in busy condition very frequently,
	 * we will check the code in status first before
	 * checking the availability of free elem.
	 *
	 * This will avoid the Q fetch/add in normal condition.
	 */
	rc = crystalhd_code_in_full(hw->adp, ioreq->uinfo.xfr_len,
				  false, data_flags);
	if (rc) {
		hw->stats.cin_busy++;
		return BC_STS_BUSY;
	}

	/* Get a list from TxFreeQ */
	tx_dma_packet = (struct tx_dma_pkt *)crystalhd_dioq_fetch(hw->tx_freeq);
	if (!tx_dma_packet) {
		BCMLOG_ERR("No empty elements..\n");
		return BC_STS_ERR_USAGE;
	}

	sts = crystalhd_xlat_sgl_to_dma_desc(ioreq,
					   &tx_dma_packet->desc_mem,
					   &dummy_index);
	if (sts != BC_STS_SUCCESS) {
		add_sts = crystalhd_dioq_add(hw->tx_freeq, tx_dma_packet,
					   false, 0);
		if (add_sts != BC_STS_SUCCESS)
			BCMLOG_ERR("double fault..\n");

		return sts;
	}

	hw->pwr_lock++;

	desc_addr.full_addr = tx_dma_packet->desc_mem.phy_addr;
	low_addr = desc_addr.low_part;
	high_addr = desc_addr.high_part;

	tx_dma_packet->call_back = call_back;
	tx_dma_packet->cb_event  = cb_event;
	tx_dma_packet->dio_req   = ioreq;

	spin_lock_irqsave(&hw->lock, flags);

	if (hw->tx_list_post_index == 0) {
		first_desc_u_addr = MISC1_TX_FIRST_DESC_U_ADDR_LIST0;
		first_desc_l_addr = MISC1_TX_FIRST_DESC_L_ADDR_LIST0;
	} else {
		first_desc_u_addr = MISC1_TX_FIRST_DESC_U_ADDR_LIST1;
		first_desc_l_addr = MISC1_TX_FIRST_DESC_L_ADDR_LIST1;
	}

	*list_id = tx_dma_packet->list_tag = hw->tx_ioq_tag_seed +
					     hw->tx_list_post_index;

	hw->tx_list_post_index = (hw->tx_list_post_index + 1) % DMA_ENGINE_CNT;

	spin_unlock_irqrestore(&hw->lock, flags);


	/* Insert in Active Q..*/
	crystalhd_dioq_add(hw->tx_actq, tx_dma_packet, false,
			 tx_dma_packet->list_tag);

	/*
	 * Interrupt will come as soon as you write
	 * the valid bit. So be ready for that. All
	 * the initialization should happen before that.
	 */
	crystalhd_start_tx_dma_engine(hw);
	crystalhd_reg_wr(hw->adp, first_desc_u_addr, desc_addr.high_part);

	crystalhd_reg_wr(hw->adp, first_desc_l_addr, desc_addr.low_part | 0x01);
					/* Be sure we set the valid bit ^^^^ */

	return BC_STS_SUCCESS;
}

/*
 * This is a force cancel and we are racing with ISR.
 *
 * Will try to remove the req from ActQ before ISR gets it.
 * If ISR gets it first then the completion happens in the
 * normal path and we will return _STS_NO_DATA from here.
 *
 * FIX_ME: Not Tested the actual condition..
 */
enum BC_STATUS crystalhd_hw_cancel_tx(struct crystalhd_hw *hw, uint32_t list_id)
{
	if (!hw || !list_id) {
		BCMLOG_ERR("Invalid Arguments\n");
		return BC_STS_INV_ARG;
	}

	crystalhd_stop_tx_dma_engine(hw);
	crystalhd_hw_tx_req_complete(hw, list_id, BC_STS_IO_USER_ABORT);

	return BC_STS_SUCCESS;
}

enum BC_STATUS crystalhd_hw_add_cap_buffer(struct crystalhd_hw *hw,
				    struct crystalhd_dio_req *ioreq, bool en_post)
{
	struct crystalhd_rx_dma_pkt *rpkt;
	uint32_t tag, uv_desc_ix = 0;
	enum BC_STATUS sts;

	if (!hw || !ioreq) {
		BCMLOG_ERR("Invalid Arguments\n");
		return BC_STS_INV_ARG;
	}

	rpkt = crystalhd_hw_alloc_rx_pkt(hw);
	if (!rpkt) {
		BCMLOG_ERR("Insufficient resources\n");
		return BC_STS_INSUFF_RES;
	}

	rpkt->dio_req = ioreq;
	tag = rpkt->pkt_tag;

	sts = crystalhd_xlat_sgl_to_dma_desc(ioreq, &rpkt->desc_mem, &uv_desc_ix);
	if (sts != BC_STS_SUCCESS)
		return sts;

	rpkt->uv_phy_addr = 0;

	/* Store the address of UV in the rx packet for post*/
	if (uv_desc_ix)
		rpkt->uv_phy_addr = rpkt->desc_mem.phy_addr +
				    (sizeof(struct dma_descriptor) * (uv_desc_ix + 1));

	if (en_post)
		sts = crystalhd_hw_post_cap_buff(hw, rpkt);
	else
		sts = crystalhd_dioq_add(hw->rx_freeq, rpkt, false, tag);

	return sts;
}

enum BC_STATUS crystalhd_hw_get_cap_buffer(struct crystalhd_hw *hw,
				    struct BC_PIC_INFO_BLOCK *pib,
				    struct crystalhd_dio_req **ioreq)
{
	struct crystalhd_rx_dma_pkt *rpkt;
	uint32_t timeout = BC_PROC_OUTPUT_TIMEOUT / 1000;
	uint32_t sig_pending = 0;


	if (!hw || !ioreq || !pib) {
		BCMLOG_ERR("Invalid Arguments\n");
		return BC_STS_INV_ARG;
	}

	rpkt = crystalhd_dioq_fetch_wait(hw->rx_rdyq, timeout, &sig_pending);
	if (!rpkt) {
		if (sig_pending) {
			BCMLOG(BCMLOG_INFO, "wait on frame time out %d\n", sig_pending);
			return BC_STS_IO_USER_ABORT;
		} else {
			return BC_STS_TIMEOUT;
		}
	}

	rpkt->dio_req->uinfo.comp_flags = rpkt->flags;

	if (rpkt->flags & COMP_FLAG_PIB_VALID)
		memcpy(pib, &rpkt->pib, sizeof(*pib));

	*ioreq = rpkt->dio_req;

	crystalhd_hw_free_rx_pkt(hw, rpkt);

	return BC_STS_SUCCESS;
}

enum BC_STATUS crystalhd_hw_start_capture(struct crystalhd_hw *hw)
{
	struct crystalhd_rx_dma_pkt *rx_pkt;
	enum BC_STATUS sts;
	uint32_t i;

	if (!hw) {
		BCMLOG_ERR("Invalid Arguments\n");
		return BC_STS_INV_ARG;
	}

	/* This is start of capture.. Post to both the lists.. */
	for (i = 0; i < DMA_ENGINE_CNT; i++) {
		rx_pkt = crystalhd_dioq_fetch(hw->rx_freeq);
		if (!rx_pkt)
			return BC_STS_NO_DATA;
		sts = crystalhd_hw_post_cap_buff(hw, rx_pkt);
		if (BC_STS_SUCCESS != sts)
			break;

	}

	return BC_STS_SUCCESS;
}

enum BC_STATUS crystalhd_hw_stop_capture(struct crystalhd_hw *hw)
{
	void *temp = NULL;

	if (!hw) {
		BCMLOG_ERR("Invalid Arguments\n");
		return BC_STS_INV_ARG;
	}

	crystalhd_stop_rx_dma_engine(hw);

	do {
		temp = crystalhd_dioq_fetch(hw->rx_freeq);
		if (temp)
			crystalhd_rx_pkt_rel_call_back(hw, temp);
	} while (temp);

	return BC_STS_SUCCESS;
}

enum BC_STATUS crystalhd_hw_pause(struct crystalhd_hw *hw)
{
	hw->stats.pause_cnt++;
	hw->stop_pending = 1;

	if ((hw->rx_list_sts[0] == sts_free) &&
	    (hw->rx_list_sts[1] == sts_free))
		crystalhd_hw_finalize_pause(hw);

	return BC_STS_SUCCESS;
}

enum BC_STATUS crystalhd_hw_unpause(struct crystalhd_hw *hw)
{
	enum BC_STATUS sts;
	uint32_t aspm;

	hw->stop_pending = 0;

	aspm = crystalhd_reg_rd(hw->adp, PCIE_DLL_DATA_LINK_CONTROL);
	aspm &= ~ASPM_L1_ENABLE;
/* NAREN BCMLOG(BCMLOG_INFO, "aspm off\n"); */
	crystalhd_reg_wr(hw->adp, PCIE_DLL_DATA_LINK_CONTROL, aspm);

	sts = crystalhd_hw_start_capture(hw);
	return sts;
}

enum BC_STATUS crystalhd_hw_suspend(struct crystalhd_hw *hw)
{
	enum BC_STATUS sts;

	if (!hw) {
		BCMLOG_ERR("Invalid Arguments\n");
		return BC_STS_INV_ARG;
	}

	sts = crystalhd_put_ddr2sleep(hw);
	if (sts != BC_STS_SUCCESS) {
		BCMLOG_ERR("Failed to Put DDR To Sleep!!\n");
		return BC_STS_ERROR;
	}

	if (!crystalhd_stop_device(hw->adp)) {
		BCMLOG_ERR("Failed to Stop Device!!\n");
		return BC_STS_ERROR;
	}

	return BC_STS_SUCCESS;
}

void crystalhd_hw_stats(struct crystalhd_hw *hw, struct crystalhd_hw_stats *stats)
{
	if (!hw) {
		BCMLOG_ERR("Invalid Arguments\n");
		return;
	}

	/* if called w/NULL stats, its a req to zero out the stats */
	if (!stats) {
		memset(&hw->stats, 0, sizeof(hw->stats));
		return;
	}

	hw->stats.freeq_count = crystalhd_dioq_count(hw->rx_freeq);
	hw->stats.rdyq_count  = crystalhd_dioq_count(hw->rx_rdyq);
	memcpy(stats, &hw->stats, sizeof(*stats));
}

enum BC_STATUS crystalhd_hw_set_core_clock(struct crystalhd_hw *hw)
{
	uint32_t reg, n, i;
	uint32_t vco_mg, refresh_reg;

	if (!hw) {
		BCMLOG_ERR("Invalid Arguments\n");
		return BC_STS_INV_ARG;
	}

	/* FIXME: jarod: wha? */
	/*n = (hw->core_clock_mhz * 3) / 20 + 1; */
	n = hw->core_clock_mhz/5;

	if (n == hw->prev_n)
		return BC_STS_CLK_NOCHG;

	if (hw->pwr_lock > 0) {
		/* BCMLOG(BCMLOG_INFO,"pwr_lock is %u\n", hw->pwr_lock) */
		return BC_STS_CLK_NOCHG;
	}

	i = n * 27;
	if (i < 560)
		vco_mg = 0;
	else if (i < 900)
		vco_mg = 1;
	else if (i < 1030)
		vco_mg = 2;
	else
		vco_mg = 3;

	reg = bc_dec_reg_rd(hw->adp, DecHt_PllACtl);

	reg &= 0xFFFFCFC0;
	reg |= n;
	reg |= vco_mg << 12;

	BCMLOG(BCMLOG_INFO, "clock is moving to %d with n %d with vco_mg %d\n",
	       hw->core_clock_mhz, n, vco_mg);

	/* Change the DRAM refresh rate to accomodate the new frequency */
	/* refresh reg = ((refresh_rate * clock_rate)/16) - 1; rounding up*/
	refresh_reg = (7 * hw->core_clock_mhz / 16);
	bc_dec_reg_wr(hw->adp, SDRAM_REF_PARAM, ((1 << 12) | refresh_reg));

	bc_dec_reg_wr(hw->adp, DecHt_PllACtl, reg);

	i = 0;

	for (i = 0; i < 10; i++) {
		reg = bc_dec_reg_rd(hw->adp, DecHt_PllACtl);

		if (reg & 0x00020000) {
			hw->prev_n = n;
			/* FIXME: jarod: outputting a random "C" is... confusing... */
			BCMLOG(BCMLOG_INFO, "C");
			return BC_STS_SUCCESS;
		} else {
			msleep_interruptible(10);
		}
	}
	BCMLOG(BCMLOG_INFO, "clk change failed\n");
	return BC_STS_CLK_NOCHG;
}
