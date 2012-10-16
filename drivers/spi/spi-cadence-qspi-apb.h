/*
 * Driver for Cadence QSPI Controller
 *
 * Copyright (C) 2012 Altera Corporation
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
 *
 */
#ifndef __CADENCE_QSPI_APB_H__
#define __CADENCE_QSPI_APB_H__

#include "spi-cadence-qspi.h"

/* Operation timeout value */
#define CQSPI_TIMEOUT_MS			(5000)
#define CQSPI_POLL_IDLE_RETRY			(3)

#define CQSPI_FIFO_WIDTH			(4)

/* Controller sram size in word */
#define CQSPI_REG_SRAM_RESV_WORDS		(2)
#define CQSPI_REG_SRAM_PARTITION_WR		(1)

#define CQSPI_REG_SRAM_THRESHOLD_BYTES		(50)

/* Instruction type */
#define CQSPI_INST_TYPE_SINGLE			(0)
#define CQSPI_INST_TYPE_DUAL			(1)
#define CQSPI_INST_TYPE_QUAD			(2)

#define CQSPI_DUMMY_CLKS_PER_BYTE		(8)
#define CQSPI_DUMMY_BYTES_MAX			(4)

#define CQSPI_STIG_DATA_LEN_MAX			(8)

#define CQSPI_INDIRECTTRIGGER_ADDR_MASK		(0xFFFFF)

/* Register map */
#define	CQSPI_REG_CONFIG			0x00
#define	CQSPI_REG_CONFIG_ENABLE_MASK		(1 << 0)
#define	CQSPI_REG_CONFIG_DECODE_MASK		(1 << 9)
#define	CQSPI_REG_CONFIG_CHIPSELECT_LSB		10
#define	CQSPI_REG_CONFIG_DMA_MASK		(1 << 15)
#define	CQSPI_REG_CONFIG_BAUD_LSB		19
#define	CQSPI_REG_CONFIG_IDLE_LSB		31
#define	CQSPI_REG_CONFIG_CHIPSELECT_MASK	0xF
#define	CQSPI_REG_CONFIG_BAUD_MASK		0xF

#define	CQSPI_REG_RD_INSTR			0x04
#define	CQSPI_REG_RD_INSTR_OPCODE_LSB		0
#define	CQSPI_REG_RD_INSTR_TYPE_INSTR_LSB	8
#define	CQSPI_REG_RD_INSTR_TYPE_ADDR_LSB	12
#define	CQSPI_REG_RD_INSTR_TYPE_DATA_LSB	16
#define	CQSPI_REG_RD_INSTR_MODE_EN_LSB		20
#define	CQSPI_REG_RD_INSTR_DUMMY_LSB		24
#define	CQSPI_REG_RD_INSTR_TYPE_INSTR_MASK	0x3
#define	CQSPI_REG_RD_INSTR_TYPE_ADDR_MASK	0x3
#define	CQSPI_REG_RD_INSTR_TYPE_DATA_MASK	0x3
#define	CQSPI_REG_RD_INSTR_DUMMY_MASK		0x1F

#define	CQSPI_REG_WR_INSTR			0x08
#define	CQSPI_REG_WR_INSTR_OPCODE_LSB		0

#define	CQSPI_REG_DELAY				0x0C
#define	CQSPI_REG_DELAY_TSLCH_LSB		0
#define	CQSPI_REG_DELAY_TCHSH_LSB		8
#define	CQSPI_REG_DELAY_TSD2D_LSB		16
#define	CQSPI_REG_DELAY_TSHSL_LSB		24
#define	CQSPI_REG_DELAY_TSLCH_MASK		0xFF
#define	CQSPI_REG_DELAY_TCHSH_MASK		0xFF
#define	CQSPI_REG_DELAY_TSD2D_MASK		0xFF
#define	CQSPI_REG_DELAY_TSHSL_MASK		0xFF

#define	CQSPI_REG_READCAPTURE			0x10
#define	CQSPI_REG_READCAPTURE_BYPASS_LSB	0
#define	CQSPI_REG_READCAPTURE_DELAY_LSB		1
#define	CQSPI_REG_READCAPTURE_DELAY_MASK	0xF

#define	CQSPI_REG_SIZE				0x14
#define	CQSPI_REG_SIZE_ADDRESS_LSB		0
#define	CQSPI_REG_SIZE_PAGE_LSB			4
#define	CQSPI_REG_SIZE_BLOCK_LSB		16
#define	CQSPI_REG_SIZE_ADDRESS_MASK		0xF
#define	CQSPI_REG_SIZE_PAGE_MASK		0xFFF
#define	CQSPI_REG_SIZE_BLOCK_MASK		0x3F

#define	CQSPI_REG_SRAMPARTITION			0x18
#define	CQSPI_REG_INDIRECTTRIGGER		0x1C

#define	CQSPI_REG_DMA				0x20
#define	CQSPI_REG_DMA_SINGLE_LSB		0
#define	CQSPI_REG_DMA_BURST_LSB			8
#define	CQSPI_REG_DMA_SINGLE_MASK		0xFF
#define	CQSPI_REG_DMA_BURST_MASK		0xFF

#define	CQSPI_REG_REMAP				0x24
#define	CQSPI_REG_MODE_BIT			0x28

#define	CQSPI_REG_SDRAMLEVEL			0x2C
#define	CQSPI_REG_SDRAMLEVEL_RD_LSB		0
#define	CQSPI_REG_SDRAMLEVEL_WR_LSB		16
#define	CQSPI_REG_SDRAMLEVEL_RD_MASK		0xFFFF
#define	CQSPI_REG_SDRAMLEVEL_WR_MASK		0xFFFF

#define	CQSPI_REG_IRQSTATUS			0x40
#define	CQSPI_REG_IRQMASK			0x44

#define	CQSPI_REG_INDIRECTRD			0x60
#define	CQSPI_REG_INDIRECTRD_START_MASK		(1 << 0)
#define	CQSPI_REG_INDIRECTRD_CANCEL_MASK	(1 << 1)
#define	CQSPI_REG_INDIRECTRD_DONE_MASK		(1 << 5)

#define	CQSPI_REG_INDIRECTRDWATERMARK		0x64
#define	CQSPI_REG_INDIRECTRDSTARTADDR		0x68
#define	CQSPI_REG_INDIRECTRDBYTES		0x6C

#define	CQSPI_REG_CMDCTRL			0x90
#define	CQSPI_REG_CMDCTRL_EXECUTE_MASK		(1 << 0)
#define	CQSPI_REG_CMDCTRL_INPROGRESS_MASK	(1 << 1)
#define	CQSPI_REG_CMDCTRL_WR_BYTES_LSB		12
#define	CQSPI_REG_CMDCTRL_WR_EN_LSB		15
#define	CQSPI_REG_CMDCTRL_ADD_BYTES_LSB		16
#define	CQSPI_REG_CMDCTRL_ADDR_EN_LSB		19
#define	CQSPI_REG_CMDCTRL_RD_BYTES_LSB		20
#define	CQSPI_REG_CMDCTRL_RD_EN_LSB		23
#define	CQSPI_REG_CMDCTRL_OPCODE_LSB		24
#define	CQSPI_REG_CMDCTRL_WR_BYTES_MASK		0x7
#define	CQSPI_REG_CMDCTRL_ADD_BYTES_MASK	0x3
#define	CQSPI_REG_CMDCTRL_RD_BYTES_MASK		0x7

#define	CQSPI_REG_INDIRECTWR			0x70
#define	CQSPI_REG_INDIRECTWR_START_MASK		(1 << 0)
#define	CQSPI_REG_INDIRECTWR_CANCEL_MASK	(1 << 1)
#define	CQSPI_REG_INDIRECTWR_DONE_MASK		(1 << 5)

#define	CQSPI_REG_INDIRECTWRWATERMARK		0x74
#define	CQSPI_REG_INDIRECTWRSTARTADDR		0x78
#define	CQSPI_REG_INDIRECTWRBYTES		0x7C

#define	CQSPI_REG_CMDADDRESS			0x94
#define	CQSPI_REG_CMDREADDATALOWER		0xA0
#define	CQSPI_REG_CMDREADDATAUPPER		0xA4
#define	CQSPI_REG_CMDWRITEDATALOWER		0xA8
#define	CQSPI_REG_CMDWRITEDATAUPPER		0xAC

/* Interrupt status bits */
#define CQSPI_REG_IRQ_MODE_ERR			(1 << 0)
#define CQSPI_REG_IRQ_UNDERFLOW			(1 << 1)
#define CQSPI_REG_IRQ_IND_COMP			(1 << 2)
#define CQSPI_REG_IRQ_IND_RD_REJECT		(1 << 3)
#define CQSPI_REG_IRQ_WR_PROTECTED_ERR		(1 << 4)
#define CQSPI_REG_IRQ_ILLEGAL_AHB_ERR		(1 << 5)
#define CQSPI_REG_IRQ_WATERMARK			(1 << 6)
#define CQSPI_REG_IRQ_IND_RD_OVERFLOW		(1 << 12)

#define CQSPI_IRQ_STATUS_ERR		(CQSPI_REG_IRQ_MODE_ERR		| \
					 CQSPI_REG_IRQ_IND_RD_REJECT	| \
					 CQSPI_REG_IRQ_WR_PROTECTED_ERR	| \
					 CQSPI_REG_IRQ_ILLEGAL_AHB_ERR)

#define CQSPI_IRQ_MASK_RD		(CQSPI_REG_IRQ_MODE_ERR		| \
					 CQSPI_REG_IRQ_IND_RD_REJECT	| \
					 CQSPI_REG_IRQ_WATERMARK	| \
					 CQSPI_REG_IRQ_IND_RD_OVERFLOW	| \
					 CQSPI_REG_IRQ_IND_COMP)

#define CQSPI_IRQ_MASK_WR		(CQSPI_REG_IRQ_MODE_ERR		| \
					 CQSPI_REG_IRQ_WR_PROTECTED_ERR	| \
					 CQSPI_REG_IRQ_IND_COMP		| \
					 CQSPI_REG_IRQ_WATERMARK	| \
					 CQSPI_REG_IRQ_UNDERFLOW)

#define CQSPI_IRQ_STATUS_MASK		(0xFFFFFFFF)

#define CQSPI_REG_IS_IDLE(base)						\
		((CQSPI_READL(base + CQSPI_REG_CONFIG) >>		\
			CQSPI_REG_CONFIG_IDLE_LSB) & 0x1)

#define CQSPI_CAL_DELAY(tdelay_ns, tref_ns, tsclk_ns)			\
		((((tdelay_ns) - (tsclk_ns)) / (tref_ns)))

#define CQSPI_GET_RD_SRAM_LEVEL(reg_basse)				\
		(((CQSPI_READL(reg_base + CQSPI_REG_SDRAMLEVEL)) >>	\
		CQSPI_REG_SDRAMLEVEL_RD_LSB) & CQSPI_REG_SDRAMLEVEL_RD_MASK)

#define CQSPI_READ_IRQ_STATUS(reg_base)					\
		CQSPI_READL(reg_base + CQSPI_REG_IRQSTATUS)

#define CQSPI_CLEAR_IRQ(reg_base, status)				\
		CQSPI_WRITEL(status, reg_base + CQSPI_REG_IRQSTATUS)

/* Functions call declaration */
unsigned int cadence_qspi_apb_is_controller_ready(void *reg_base_addr);
void cadence_qspi_apb_controller_init(struct struct_cqspi *cadence_qspi);
int cadence_qspi_apb_process_queue(struct struct_cqspi *cadence_qspi,
	struct spi_device *spi, unsigned int n_trans,
	struct spi_transfer **spi_xfer);
void cadence_qspi_apb_controller_enable(void *reg_base_addr);
void cadence_qspi_apb_controller_disable(void *reg_base_addr);
#endif /* __CADENCE_QSPI_APB_H__ */
