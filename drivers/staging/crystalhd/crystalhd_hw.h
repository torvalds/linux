/***************************************************************************
 * Copyright (c) 2005-2009, Broadcom Corporation.
 *
 *  Name: crystalhd_hw . h
 *
 *  Description:
 *		BCM70012 Linux driver hardware layer.
 *
 *  HISTORY:
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

#ifndef _CRYSTALHD_HW_H_
#define _CRYSTALHD_HW_H_

#include "crystalhd.h"

/* HW constants..*/
#define DMA_ENGINE_CNT		2
#define MAX_PIB_Q_DEPTH		64
#define MIN_PIB_Q_DEPTH		2
#define WR_POINTER_OFF		4

#define ASPM_L1_ENABLE		(BC_BIT(27))

/*************************************************
  7412 Decoder  Registers.
**************************************************/
#define FW_CMD_BUFF_SZ		64
#define TS_Host2CpuSnd		0x00000100
#define Hst2CpuMbx1		0x00100F00
#define Cpu2HstMbx1		0x00100F04
#define MbxStat1		0x00100F08
#define Stream2Host_Intr_Sts	0x00100F24
#define C011_RET_SUCCESS	0x0 /* Reutrn status of firmware command. */

/* TS input status register */
#define TS_StreamAFIFOStatus	0x0010044C
#define TS_StreamBFIFOStatus	0x0010084C

/*UART Selection definitions*/
#define UartSelectA		0x00100300
#define UartSelectB		0x00100304

#define BSVS_UART_DEC_NONE	0x00
#define BSVS_UART_DEC_OUTER	0x01
#define BSVS_UART_DEC_INNER	0x02
#define BSVS_UART_STREAM	0x03

/* Code-In fifo */
#define REG_DecCA_RegCinCTL	0xa00
#define REG_DecCA_RegCinBase	0xa0c
#define REG_DecCA_RegCinEnd	0xa10
#define REG_DecCA_RegCinWrPtr	0xa04
#define REG_DecCA_RegCinRdPtr	0xa08

#define REG_Dec_TsUser0Base	0x100864
#define REG_Dec_TsUser0Rdptr	0x100868
#define REG_Dec_TsUser0Wrptr	0x10086C
#define REG_Dec_TsUser0End	0x100874

/* ASF Case ...*/
#define REG_Dec_TsAudCDB2Base	0x10036c
#define REG_Dec_TsAudCDB2Rdptr  0x100378
#define REG_Dec_TsAudCDB2Wrptr  0x100374
#define REG_Dec_TsAudCDB2End	0x100370

/* DRAM bringup Registers */
#define SDRAM_PARAM		0x00040804
#define SDRAM_PRECHARGE		0x000408B0
#define SDRAM_EXT_MODE		0x000408A4
#define SDRAM_MODE		0x000408A0
#define SDRAM_REFRESH		0x00040890
#define SDRAM_REF_PARAM		0x00040808

#define DecHt_PllACtl		0x34000C
#define DecHt_PllBCtl		0x340010
#define DecHt_PllCCtl		0x340014
#define DecHt_PllDCtl		0x340034
#define DecHt_PllECtl		0x340038
#define AUD_DSP_MISC_SOFT_RESET	0x00240104
#define AIO_MISC_PLL_RESET	0x0026000C
#define PCIE_CLK_REQ_REG	0xDC
#define	PCI_CLK_REQ_ENABLE	(BC_BIT(8))

/*************************************************
  F/W Copy engine definitions..
**************************************************/
#define BC_FWIMG_ST_ADDR	0x00000000
/* FIXME: jarod: there's a kernel function that'll do this for us... */
#define rotr32_1(x, n)		(((x) >> n) | ((x) << (32 - n)))
#define bswap_32_1(x) ((rotr32_1((x), 24) & 0x00ff00ff) | (rotr32_1((x), 8) & 0xff00ff00))

#define DecHt_HostSwReset	0x340000
#define BC_DRAM_FW_CFG_ADDR	0x001c2000

union addr_64 {
	struct {
		uint32_t	low_part;
		uint32_t	high_part;
	};

	uint64_t	full_addr;

};

union intr_mask_reg {
	struct {
		uint32_t	mask_tx_done:1;
		uint32_t	mask_tx_err:1;
		uint32_t	mask_rx_done:1;
		uint32_t	mask_rx_err:1;
		uint32_t	mask_pcie_err:1;
		uint32_t	mask_pcie_rbusmast_err:1;
		uint32_t	mask_pcie_rgr_bridge:1;
		uint32_t	reserved:25;
	};

	uint32_t	whole_reg;

};

union link_misc_perst_deco_ctrl {
	struct {
		uint32_t	bcm7412_rst:1;	/* 1 -> BCM7412 is held
						in reset. Reset value 1.*/
		uint32_t	reserved0:3;		/* Reserved.No Effect*/
		uint32_t	stop_bcm_7412_clk:1;	/* 1 ->Stops branch of
						27MHz clk used to clk BCM7412*/
		uint32_t	reserved1:27;		/* Reseved. No Effect*/
	};

	uint32_t	whole_reg;

};

union link_misc_perst_clk_ctrl {
	struct {
		uint32_t	sel_alt_clk:1;	  /* When set, selects a
				 6.75MHz clock as the source of core_clk */
		uint32_t	stop_core_clk:1;  /* When set, stops the branch
		 of core_clk that is not needed for low power operation */
		uint32_t	pll_pwr_dn:1;	  /* When set, powers down the
			 main PLL. The alternate clock bit should be set to
			 select an alternate clock before setting this bit.*/
		uint32_t	reserved0:5;	  /* Reserved */
		uint32_t	pll_mult:8;	  /* This setting controls
						 the multiplier for the PLL. */
		uint32_t	pll_div:4;	  /* This setting controls
						 the divider for the PLL. */
		uint32_t	reserved1:12;	  /* Reserved */
	};

	uint32_t	whole_reg;

};

union link_misc_perst_decoder_ctrl {
	struct {
		uint32_t	bcm_7412_rst:1; /* 1 -> BCM7412 is held
						 in reset. Reset value 1.*/
		uint32_t	res0:3; /* Reserved.No Effect*/
		uint32_t	stop_7412_clk:1; /* 1 ->Stops branch of 27MHz
						 clk used to clk BCM7412*/
		uint32_t	res1:27; /* Reseved. No Effect */
	};

	uint32_t	whole_reg;

};

union desc_low_addr_reg {
	struct {
		uint32_t	list_valid:1;
		uint32_t	reserved:4;
		uint32_t	low_addr:27;
	};

	uint32_t	whole_reg;

};

struct dma_descriptor {	/* 8 32-bit values */
	/* 0th u32 */
	uint32_t sdram_buff_addr:28;	/* bits 0-27:  SDRAM Address */
	uint32_t res0:4;		/* bits 28-31: Reserved */

	/* 1st u32 */
	uint32_t buff_addr_low;		/* 1 buffer address low */
	uint32_t buff_addr_high;	/* 2 buffer address high */

	/* 3rd u32 */
	uint32_t res2:2;		/* 0-1 - Reserved */
	uint32_t xfer_size:23;		/* 2-24 = Xfer size in words */
	uint32_t res3:6;		/* 25-30 reserved */
	uint32_t intr_enable:1;		/* 31 - Interrupt After this desc */

	/* 4th u32 */
	uint32_t endian_xlat_align:2;	/* 0-1 Endian Translation */
	uint32_t next_desc_cont:1;	/* 2 - Next desc is in contig memory */
	uint32_t res4:25;		/* 3 - 27 Reserved bits */
	uint32_t fill_bytes:2;		/* 28-29 Bits Fill Bytes */
	uint32_t dma_dir:1;		/* 30 bit DMA Direction */
	uint32_t last_rec_indicator:1;	/* 31 bit Last Record Indicator */

	/* 5th u32 */
	uint32_t next_desc_addr_low;	/* 32-bits Next Desc Addr lower */

	/* 6th u32 */
	uint32_t next_desc_addr_high;	/* 32-bits Next Desc Addr Higher */

	/* 7th u32 */
	uint32_t res8;			/* Last 32bits reserved */

};

/*
 * We will allocate the memory in 4K pages
 * the linked list will be a list of 32 byte descriptors.
 * The  virtual address will determine what should be freed.
 */
struct dma_desc_mem {
	struct dma_descriptor	*pdma_desc_start; /* 32-bytes for dma
				 descriptor. should be first element */
	dma_addr_t		phy_addr;	/* physical address
						 of each DMA desc */
	uint32_t		sz;
	struct _dma_desc_mem_	*Next; /* points to Next Descriptor in chain */

};

enum list_sts {
	sts_free = 0,

	/* RX-Y Bits 0:7 */
	rx_waiting_y_intr	= 0x00000001,
	rx_y_error		= 0x00000004,

	/* RX-UV Bits 8:16 */
	rx_waiting_uv_intr	= 0x0000100,
	rx_uv_error		= 0x0000400,

	rx_sts_waiting		= (rx_waiting_y_intr|rx_waiting_uv_intr),
	rx_sts_error		= (rx_y_error|rx_uv_error),

	rx_y_mask		= 0x000000FF,
	rx_uv_mask		= 0x0000FF00,
};

struct tx_dma_pkt {
	struct dma_desc_mem	desc_mem;
	hw_comp_callback	call_back;
	struct crystalhd_dio_req	*dio_req;
	wait_queue_head_t	*cb_event;
	uint32_t		list_tag;
};

struct crystalhd_rx_dma_pkt {
	struct dma_desc_mem		desc_mem;
	struct crystalhd_dio_req	*dio_req;
	uint32_t			pkt_tag;
	uint32_t			flags;
	struct BC_PIC_INFO_BLOCK	pib;
	dma_addr_t			uv_phy_addr;
	struct crystalhd_rx_dma_pkt	*next;
};

struct crystalhd_hw_stats {
	uint32_t	rx_errors;
	uint32_t	tx_errors;
	uint32_t	freeq_count;
	uint32_t	rdyq_count;
	uint32_t	num_interrupts;
	uint32_t	dev_interrupts;
	uint32_t	cin_busy;
	uint32_t	pause_cnt;
};

struct crystalhd_hw {
	struct tx_dma_pkt	tx_pkt_pool[DMA_ENGINE_CNT];
	spinlock_t		lock;

	uint32_t		tx_ioq_tag_seed;
	uint32_t		tx_list_post_index;

	struct crystalhd_rx_dma_pkt *rx_pkt_pool_head;
	uint32_t		rx_pkt_tag_seed;

	bool			dev_started;
	void			*adp;

	wait_queue_head_t	*pfw_cmd_event;
	int			fwcmd_evt_sts;

	uint32_t		pib_del_Q_addr;
	uint32_t		pib_rel_Q_addr;

	struct crystalhd_dioq	*tx_freeq;
	struct crystalhd_dioq	*tx_actq;

	/* Rx DMA Engine Specific Locks */
	spinlock_t		rx_lock;
	uint32_t		rx_list_post_index;
	enum list_sts		rx_list_sts[DMA_ENGINE_CNT];
	struct crystalhd_dioq	*rx_rdyq;
	struct crystalhd_dioq	*rx_freeq;
	struct crystalhd_dioq	*rx_actq;
	uint32_t		stop_pending;

	/* HW counters.. */
	struct crystalhd_hw_stats	stats;

	/* Core clock in MHz */
	uint32_t		core_clock_mhz;
	uint32_t		prev_n;
	uint32_t		pwr_lock;
};

/* Clock defines for power control */
#define CLOCK_PRESET 175

/* DMA engine register BIT mask wrappers.. */
#define DMA_START_BIT	MISC1_TX_SW_DESC_LIST_CTRL_STS_TX_DMA_RUN_STOP_MASK

#define GET_RX_INTR_MASK (INTR_INTR_STATUS_L1_UV_RX_DMA_ERR_INTR_MASK |	\
	INTR_INTR_STATUS_L1_UV_RX_DMA_DONE_INTR_MASK |	\
	INTR_INTR_STATUS_L1_Y_RX_DMA_ERR_INTR_MASK |		\
	INTR_INTR_STATUS_L1_Y_RX_DMA_DONE_INTR_MASK |		\
	INTR_INTR_STATUS_L0_UV_RX_DMA_ERR_INTR_MASK |		\
	INTR_INTR_STATUS_L0_UV_RX_DMA_DONE_INTR_MASK |	\
	INTR_INTR_STATUS_L0_Y_RX_DMA_ERR_INTR_MASK |		\
	INTR_INTR_STATUS_L0_Y_RX_DMA_DONE_INTR_MASK)

#define GET_Y0_ERR_MSK (MISC1_Y_RX_ERROR_STATUS_RX_L0_OVERRUN_ERROR_MASK | \
	MISC1_Y_RX_ERROR_STATUS_RX_L0_UNDERRUN_ERROR_MASK |		\
	MISC1_Y_RX_ERROR_STATUS_RX_L0_DESC_TX_ABORT_ERRORS_MASK |	\
	MISC1_Y_RX_ERROR_STATUS_RX_L0_FIFO_FULL_ERRORS_MASK)

#define GET_UV0_ERR_MSK (MISC1_UV_RX_ERROR_STATUS_RX_L0_OVERRUN_ERROR_MASK | \
	MISC1_UV_RX_ERROR_STATUS_RX_L0_UNDERRUN_ERROR_MASK |		\
	MISC1_UV_RX_ERROR_STATUS_RX_L0_DESC_TX_ABORT_ERRORS_MASK |	\
	MISC1_UV_RX_ERROR_STATUS_RX_L0_FIFO_FULL_ERRORS_MASK)

#define GET_Y1_ERR_MSK (MISC1_Y_RX_ERROR_STATUS_RX_L1_OVERRUN_ERROR_MASK | \
	MISC1_Y_RX_ERROR_STATUS_RX_L1_UNDERRUN_ERROR_MASK |		\
	MISC1_Y_RX_ERROR_STATUS_RX_L1_DESC_TX_ABORT_ERRORS_MASK |	\
	MISC1_Y_RX_ERROR_STATUS_RX_L1_FIFO_FULL_ERRORS_MASK)

#define GET_UV1_ERR_MSK	(MISC1_UV_RX_ERROR_STATUS_RX_L1_OVERRUN_ERROR_MASK | \
	MISC1_UV_RX_ERROR_STATUS_RX_L1_UNDERRUN_ERROR_MASK |		\
	MISC1_UV_RX_ERROR_STATUS_RX_L1_DESC_TX_ABORT_ERRORS_MASK |	\
	MISC1_UV_RX_ERROR_STATUS_RX_L1_FIFO_FULL_ERRORS_MASK)


/**** API Exposed to the other layers ****/
enum BC_STATUS crystalhd_download_fw(struct crystalhd_adp *adp,
			      void *buffer, uint32_t sz);
enum BC_STATUS crystalhd_do_fw_cmd(struct crystalhd_hw *hw,
				 struct BC_FW_CMD *fw_cmd);
bool crystalhd_hw_interrupt(struct crystalhd_adp *adp,
				 struct crystalhd_hw *hw);
enum BC_STATUS crystalhd_hw_open(struct crystalhd_hw *,
				 struct crystalhd_adp *);
enum BC_STATUS crystalhd_hw_close(struct crystalhd_hw *);
enum BC_STATUS crystalhd_hw_setup_dma_rings(struct crystalhd_hw *);
enum BC_STATUS crystalhd_hw_free_dma_rings(struct crystalhd_hw *);


enum BC_STATUS crystalhd_hw_post_tx(struct crystalhd_hw *hw,
			     struct crystalhd_dio_req *ioreq,
			     hw_comp_callback call_back,
			     wait_queue_head_t *cb_event,
			     uint32_t *list_id, uint8_t data_flags);

enum BC_STATUS crystalhd_hw_pause(struct crystalhd_hw *hw);
enum BC_STATUS crystalhd_hw_unpause(struct crystalhd_hw *hw);
enum BC_STATUS crystalhd_hw_suspend(struct crystalhd_hw *hw);
enum BC_STATUS crystalhd_hw_cancel_tx(struct crystalhd_hw *hw,
				 uint32_t list_id);
enum BC_STATUS crystalhd_hw_add_cap_buffer(struct crystalhd_hw *hw,
			 struct crystalhd_dio_req *ioreq, bool en_post);
enum BC_STATUS crystalhd_hw_get_cap_buffer(struct crystalhd_hw *hw,
				    struct BC_PIC_INFO_BLOCK *pib,
				    struct crystalhd_dio_req **ioreq);
enum BC_STATUS crystalhd_hw_stop_capture(struct crystalhd_hw *hw);
enum BC_STATUS crystalhd_hw_start_capture(struct crystalhd_hw *hw);
void crystalhd_hw_stats(struct crystalhd_hw *hw,
			 struct crystalhd_hw_stats *stats);

/* API to program the core clock on the decoder */
enum BC_STATUS crystalhd_hw_set_core_clock(struct crystalhd_hw *);

#endif
