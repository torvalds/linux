// SPDX-License-Identifier: GPL-2.0+
// Cadence XSPI flash controller driver
// Copyright (C) 2020-21 Cadence

#include <linux/acpi.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>
#include <linux/bitfield.h>
#include <linux/limits.h>
#include <linux/log2.h>
#include <linux/bitrev.h>

#define CDNS_XSPI_MAGIC_NUM_VALUE	0x6522
#define CDNS_XSPI_MAX_BANKS		8
#define CDNS_XSPI_NAME			"cadence-xspi"

/*
 * Note: below are additional auxiliary registers to
 * configure XSPI controller pin-strap settings
 */

/* PHY DQ timing register */
#define CDNS_XSPI_CCP_PHY_DQ_TIMING		0x0000

/* PHY DQS timing register */
#define CDNS_XSPI_CCP_PHY_DQS_TIMING		0x0004

/* PHY gate loopback control register */
#define CDNS_XSPI_CCP_PHY_GATE_LPBCK_CTRL	0x0008

/* PHY DLL slave control register */
#define CDNS_XSPI_CCP_PHY_DLL_SLAVE_CTRL	0x0010

/* DLL PHY control register */
#define CDNS_XSPI_DLL_PHY_CTRL			0x1034

/* Command registers */
#define CDNS_XSPI_CMD_REG_0			0x0000
#define CDNS_XSPI_CMD_REG_1			0x0004
#define CDNS_XSPI_CMD_REG_2			0x0008
#define CDNS_XSPI_CMD_REG_3			0x000C
#define CDNS_XSPI_CMD_REG_4			0x0010
#define CDNS_XSPI_CMD_REG_5			0x0014

/* Command status registers */
#define CDNS_XSPI_CMD_STATUS_REG		0x0044

/* Controller status register */
#define CDNS_XSPI_CTRL_STATUS_REG		0x0100
#define CDNS_XSPI_INIT_COMPLETED		BIT(16)
#define CDNS_XSPI_INIT_LEGACY			BIT(9)
#define CDNS_XSPI_INIT_FAIL			BIT(8)
#define CDNS_XSPI_CTRL_BUSY			BIT(7)

/* Controller interrupt status register */
#define CDNS_XSPI_INTR_STATUS_REG		0x0110
#define CDNS_XSPI_STIG_DONE			BIT(23)
#define CDNS_XSPI_SDMA_ERROR			BIT(22)
#define CDNS_XSPI_SDMA_TRIGGER			BIT(21)
#define CDNS_XSPI_CMD_IGNRD_EN			BIT(20)
#define CDNS_XSPI_DDMA_TERR_EN			BIT(18)
#define CDNS_XSPI_CDMA_TREE_EN			BIT(17)
#define CDNS_XSPI_CTRL_IDLE_EN			BIT(16)

#define CDNS_XSPI_TRD_COMP_INTR_STATUS		0x0120
#define CDNS_XSPI_TRD_ERR_INTR_STATUS		0x0130
#define CDNS_XSPI_TRD_ERR_INTR_EN		0x0134

/* Controller interrupt enable register */
#define CDNS_XSPI_INTR_ENABLE_REG		0x0114
#define CDNS_XSPI_INTR_EN			BIT(31)
#define CDNS_XSPI_STIG_DONE_EN			BIT(23)
#define CDNS_XSPI_SDMA_ERROR_EN			BIT(22)
#define CDNS_XSPI_SDMA_TRIGGER_EN		BIT(21)

#define CDNS_XSPI_INTR_MASK (CDNS_XSPI_INTR_EN | \
	CDNS_XSPI_STIG_DONE_EN  | \
	CDNS_XSPI_SDMA_ERROR_EN | \
	CDNS_XSPI_SDMA_TRIGGER_EN)

/* Controller config register */
#define CDNS_XSPI_CTRL_CONFIG_REG		0x0230
#define CDNS_XSPI_CTRL_WORK_MODE		GENMASK(6, 5)

#define CDNS_XSPI_WORK_MODE_DIRECT		0
#define CDNS_XSPI_WORK_MODE_STIG		1
#define CDNS_XSPI_WORK_MODE_ACMD		3

/* SDMA trigger transaction registers */
#define CDNS_XSPI_SDMA_SIZE_REG			0x0240
#define CDNS_XSPI_SDMA_TRD_INFO_REG		0x0244
#define CDNS_XSPI_SDMA_DIR			BIT(8)

/* Controller features register */
#define CDNS_XSPI_CTRL_FEATURES_REG		0x0F04
#define CDNS_XSPI_NUM_BANKS			GENMASK(25, 24)
#define CDNS_XSPI_DMA_DATA_WIDTH		BIT(21)
#define CDNS_XSPI_NUM_THREADS			GENMASK(3, 0)

/* Controller version register */
#define CDNS_XSPI_CTRL_VERSION_REG		0x0F00
#define CDNS_XSPI_MAGIC_NUM			GENMASK(31, 16)
#define CDNS_XSPI_CTRL_REV			GENMASK(7, 0)

/* STIG Profile 1.0 instruction fields (split into registers) */
#define CDNS_XSPI_CMD_INSTR_TYPE		GENMASK(6, 0)
#define CDNS_XSPI_CMD_P1_R1_ADDR0		GENMASK(31, 24)
#define CDNS_XSPI_CMD_P1_R2_ADDR1		GENMASK(7, 0)
#define CDNS_XSPI_CMD_P1_R2_ADDR2		GENMASK(15, 8)
#define CDNS_XSPI_CMD_P1_R2_ADDR3		GENMASK(23, 16)
#define CDNS_XSPI_CMD_P1_R2_ADDR4		GENMASK(31, 24)
#define CDNS_XSPI_CMD_P1_R3_ADDR5		GENMASK(7, 0)
#define CDNS_XSPI_CMD_P1_R3_CMD			GENMASK(23, 16)
#define CDNS_XSPI_CMD_P1_R3_NUM_ADDR_BYTES	GENMASK(30, 28)
#define CDNS_XSPI_CMD_P1_R4_ADDR_IOS		GENMASK(1, 0)
#define CDNS_XSPI_CMD_P1_R4_CMD_IOS		GENMASK(9, 8)
#define CDNS_XSPI_CMD_P1_R4_BANK		GENMASK(14, 12)

/* STIG data sequence instruction fields (split into registers) */
#define CDNS_XSPI_CMD_DSEQ_R2_DCNT_L		GENMASK(31, 16)
#define CDNS_XSPI_CMD_DSEQ_R3_DCNT_H		GENMASK(15, 0)
#define CDNS_XSPI_CMD_DSEQ_R3_NUM_OF_DUMMY	GENMASK(25, 20)
#define CDNS_XSPI_CMD_DSEQ_R4_BANK		GENMASK(14, 12)
#define CDNS_XSPI_CMD_DSEQ_R4_DATA_IOS		GENMASK(9, 8)
#define CDNS_XSPI_CMD_DSEQ_R4_DIR		BIT(4)

/* STIG command status fields */
#define CDNS_XSPI_CMD_STATUS_COMPLETED		BIT(15)
#define CDNS_XSPI_CMD_STATUS_FAILED		BIT(14)
#define CDNS_XSPI_CMD_STATUS_DQS_ERROR		BIT(3)
#define CDNS_XSPI_CMD_STATUS_CRC_ERROR		BIT(2)
#define CDNS_XSPI_CMD_STATUS_BUS_ERROR		BIT(1)
#define CDNS_XSPI_CMD_STATUS_INV_SEQ_ERROR	BIT(0)

#define CDNS_XSPI_STIG_DONE_FLAG		BIT(0)
#define CDNS_XSPI_TRD_STATUS			0x0104

#define MODE_NO_OF_BYTES			GENMASK(25, 24)
#define MODEBYTES_COUNT			1

/* Helper macros for filling command registers */
#define CDNS_XSPI_CMD_FLD_P1_INSTR_CMD_1(op, data_phase) ( \
	FIELD_PREP(CDNS_XSPI_CMD_INSTR_TYPE, (data_phase) ? \
		CDNS_XSPI_STIG_INSTR_TYPE_1 : CDNS_XSPI_STIG_INSTR_TYPE_0) | \
	FIELD_PREP(CDNS_XSPI_CMD_P1_R1_ADDR0, (op)->addr.val & 0xff))

#define CDNS_XSPI_CMD_FLD_P1_INSTR_CMD_2(op) ( \
	FIELD_PREP(CDNS_XSPI_CMD_P1_R2_ADDR1, ((op)->addr.val >> 8)  & 0xFF) | \
	FIELD_PREP(CDNS_XSPI_CMD_P1_R2_ADDR2, ((op)->addr.val >> 16) & 0xFF) | \
	FIELD_PREP(CDNS_XSPI_CMD_P1_R2_ADDR3, ((op)->addr.val >> 24) & 0xFF) | \
	FIELD_PREP(CDNS_XSPI_CMD_P1_R2_ADDR4, ((op)->addr.val >> 32) & 0xFF))

#define CDNS_XSPI_CMD_FLD_P1_INSTR_CMD_3(op, modebytes) ( \
	FIELD_PREP(CDNS_XSPI_CMD_P1_R3_ADDR5, ((op)->addr.val >> 40) & 0xFF) | \
	FIELD_PREP(CDNS_XSPI_CMD_P1_R3_CMD, (op)->cmd.opcode) | \
	FIELD_PREP(MODE_NO_OF_BYTES, modebytes) | \
	FIELD_PREP(CDNS_XSPI_CMD_P1_R3_NUM_ADDR_BYTES, (op)->addr.nbytes))

#define CDNS_XSPI_CMD_FLD_P1_INSTR_CMD_4(op, chipsel) ( \
	FIELD_PREP(CDNS_XSPI_CMD_P1_R4_ADDR_IOS, ilog2((op)->addr.buswidth)) | \
	FIELD_PREP(CDNS_XSPI_CMD_P1_R4_CMD_IOS, ilog2((op)->cmd.buswidth)) | \
	FIELD_PREP(CDNS_XSPI_CMD_P1_R4_BANK, chipsel))

#define CDNS_XSPI_CMD_FLD_DSEQ_CMD_1(op) \
	FIELD_PREP(CDNS_XSPI_CMD_INSTR_TYPE, CDNS_XSPI_STIG_INSTR_TYPE_DATA_SEQ)

#define CDNS_XSPI_CMD_FLD_DSEQ_CMD_2(op) \
	FIELD_PREP(CDNS_XSPI_CMD_DSEQ_R2_DCNT_L, (op)->data.nbytes & 0xFFFF)

#define CDNS_XSPI_CMD_FLD_DSEQ_CMD_3(op, dummybytes) ( \
	FIELD_PREP(CDNS_XSPI_CMD_DSEQ_R3_DCNT_H, \
		((op)->data.nbytes >> 16) & 0xffff) | \
	FIELD_PREP(CDNS_XSPI_CMD_DSEQ_R3_NUM_OF_DUMMY, \
		  (op)->dummy.buswidth != 0 ? \
		  (((dummybytes) * 8) / (op)->dummy.buswidth) : \
		  0))

#define CDNS_XSPI_CMD_FLD_DSEQ_CMD_4(op, chipsel) ( \
	FIELD_PREP(CDNS_XSPI_CMD_DSEQ_R4_BANK, chipsel) | \
	FIELD_PREP(CDNS_XSPI_CMD_DSEQ_R4_DATA_IOS, \
		ilog2((op)->data.buswidth)) | \
	FIELD_PREP(CDNS_XSPI_CMD_DSEQ_R4_DIR, \
		((op)->data.dir == SPI_MEM_DATA_IN) ? \
		CDNS_XSPI_STIG_CMD_DIR_READ : CDNS_XSPI_STIG_CMD_DIR_WRITE))

/* Helper macros for GENERIC and GENERIC-DSEQ instruction type */
#define CMD_REG_LEN (6*4)
#define INSTRUCTION_TYPE_GENERIC 96
#define CDNS_XSPI_CMD_FLD_P1_GENERIC_CMD (\
	FIELD_PREP(CDNS_XSPI_CMD_INSTR_TYPE, INSTRUCTION_TYPE_GENERIC))

#define GENERIC_NUM_OF_BYTES GENMASK(27, 24)
#define CDNS_XSPI_CMD_FLD_P3_GENERIC_CMD(len) (\
	FIELD_PREP(GENERIC_NUM_OF_BYTES, len))

#define GENERIC_BANK_NUM GENMASK(14, 12)
#define GENERIC_GLUE_CMD BIT(28)
#define CDNS_XSPI_CMD_FLD_P4_GENERIC_CMD(cs, glue) (\
	FIELD_PREP(GENERIC_BANK_NUM, cs) | FIELD_PREP(GENERIC_GLUE_CMD, glue))

#define CDNS_XSPI_CMD_FLD_GENERIC_DSEQ_CMD_1 (\
	FIELD_PREP(CDNS_XSPI_CMD_INSTR_TYPE, CDNS_XSPI_STIG_INSTR_TYPE_DATA_SEQ))

#define CDNS_XSPI_CMD_FLD_GENERIC_DSEQ_CMD_2(nbytes) (\
	FIELD_PREP(CDNS_XSPI_CMD_DSEQ_R2_DCNT_L, nbytes & 0xffff))

#define CDNS_XSPI_CMD_FLD_GENERIC_DSEQ_CMD_3(nbytes) ( \
	FIELD_PREP(CDNS_XSPI_CMD_DSEQ_R3_DCNT_H, (nbytes >> 16) & 0xffff))

#define CDNS_XSPI_CMD_FLD_GENERIC_DSEQ_CMD_4(dir, chipsel) ( \
	FIELD_PREP(CDNS_XSPI_CMD_DSEQ_R4_BANK, chipsel) | \
	FIELD_PREP(CDNS_XSPI_CMD_DSEQ_R4_DIR, dir))

/* Marvell PHY default values */
#define MARVELL_REGS_DLL_PHY_CTRL		0x00000707
#define MARVELL_CTB_RFILE_PHY_CTRL		0x00004000
#define MARVELL_RFILE_PHY_TSEL			0x00000000
#define MARVELL_RFILE_PHY_DQ_TIMING		0x00000101
#define MARVELL_RFILE_PHY_DQS_TIMING		0x00700404
#define MARVELL_RFILE_PHY_GATE_LPBK_CTRL	0x00200030
#define MARVELL_RFILE_PHY_DLL_MASTER_CTRL	0x00800000
#define MARVELL_RFILE_PHY_DLL_SLAVE_CTRL	0x0000ff01

/* PHY config registers */
#define CDNS_XSPI_RF_MINICTRL_REGS_DLL_PHY_CTRL			0x1034
#define CDNS_XSPI_PHY_CTB_RFILE_PHY_CTRL			0x0080
#define CDNS_XSPI_PHY_CTB_RFILE_PHY_TSEL			0x0084
#define CDNS_XSPI_PHY_DATASLICE_RFILE_PHY_DQ_TIMING		0x0000
#define CDNS_XSPI_PHY_DATASLICE_RFILE_PHY_DQS_TIMING		0x0004
#define CDNS_XSPI_PHY_DATASLICE_RFILE_PHY_GATE_LPBK_CTRL	0x0008
#define CDNS_XSPI_PHY_DATASLICE_RFILE_PHY_DLL_MASTER_CTRL	0x000c
#define CDNS_XSPI_PHY_DATASLICE_RFILE_PHY_DLL_SLAVE_CTRL	0x0010
#define CDNS_XSPI_DATASLICE_RFILE_PHY_DLL_OBS_REG_0		0x001c

#define CDNS_XSPI_DLL_RST_N BIT(24)
#define CDNS_XSPI_DLL_LOCK  BIT(0)

/* Marvell overlay registers - clock */
#define MRVL_XSPI_CLK_CTRL_AUX_REG   0x2020
#define MRVL_XSPI_CLK_ENABLE	     BIT(0)
#define MRVL_XSPI_CLK_DIV	     GENMASK(4, 1)
#define MRVL_XSPI_IRQ_ENABLE	     BIT(6)
#define MRVL_XSPI_CLOCK_IO_HZ	     800000000
#define MRVL_XSPI_CLOCK_DIVIDED(div) ((MRVL_XSPI_CLOCK_IO_HZ) / (div))
#define MRVL_DEFAULT_CLK	     25000000

/* Marvell overlay registers - xfer */
#define MRVL_XFER_FUNC_CTRL		 0x210
#define MRVL_XFER_FUNC_CTRL_READ_DATA(i) (0x000 + 8 * (i))
#define MRVL_XFER_SOFT_RESET		 BIT(11)
#define MRVL_XFER_CS_N_HOLD		 GENMASK(9, 6)
#define MRVL_XFER_RECEIVE_ENABLE	 BIT(4)
#define MRVL_XFER_FUNC_ENABLE		 BIT(3)
#define MRVL_XFER_CLK_CAPTURE_POL	 BIT(2)
#define MRVL_XFER_CLK_DRIVE_POL		 BIT(1)
#define MRVL_XFER_FUNC_START		 BIT(0)
#define MRVL_XFER_QWORD_COUNT		 32
#define MRVL_XFER_QWORD_BYTECOUNT	 8

#define MRVL_XSPI_POLL_TIMEOUT_US	1000
#define MRVL_XSPI_POLL_DELAY_US		10

/* Macros for calculating data bits in generic command
 * Up to 10 bytes can be fit into cmd_registers
 * least significant is placed in cmd_reg[1]
 * Other bits are inserted after it in cmd_reg[1,2,3] register
 */
#define GENERIC_CMD_DATA_REG_3_COUNT(len)	(len >= 10 ? 2 : len - 8)
#define GENERIC_CMD_DATA_REG_2_COUNT(len)	(len >= 7 ? 3 : len - 4)
#define GENERIC_CMD_DATA_REG_1_COUNT(len)	(len >= 3 ? 2 : len - 1)
#define GENERIC_CMD_DATA_3_OFFSET(position)	(8*(position))
#define GENERIC_CMD_DATA_2_OFFSET(position)	(8*(position))
#define GENERIC_CMD_DATA_1_OFFSET(position)	(8 + 8*(position))
#define GENERIC_CMD_DATA_INSERT(data, pos)	((data) << (pos))
#define GENERIC_CMD_REG_3_NEEDED(len)		(len > 7)
#define GENERIC_CMD_REG_2_NEEDED(len)		(len > 3)

enum cdns_xspi_stig_instr_type {
	CDNS_XSPI_STIG_INSTR_TYPE_0,
	CDNS_XSPI_STIG_INSTR_TYPE_1,
	CDNS_XSPI_STIG_INSTR_TYPE_DATA_SEQ = 127,
};

enum cdns_xspi_sdma_dir {
	CDNS_XSPI_SDMA_DIR_READ,
	CDNS_XSPI_SDMA_DIR_WRITE,
};

enum cdns_xspi_stig_cmd_dir {
	CDNS_XSPI_STIG_CMD_DIR_READ,
	CDNS_XSPI_STIG_CMD_DIR_WRITE,
};

struct cdns_xspi_driver_data {
	bool mrvl_hw_overlay;
	u32 dll_phy_ctrl;
	u32 ctb_rfile_phy_ctrl;
	u32 rfile_phy_tsel;
	u32 rfile_phy_dq_timing;
	u32 rfile_phy_dqs_timing;
	u32 rfile_phy_gate_lpbk_ctrl;
	u32 rfile_phy_dll_master_ctrl;
	u32 rfile_phy_dll_slave_ctrl;
};

static struct cdns_xspi_driver_data marvell_driver_data = {
	.mrvl_hw_overlay = true,
	.dll_phy_ctrl = MARVELL_REGS_DLL_PHY_CTRL,
	.ctb_rfile_phy_ctrl = MARVELL_CTB_RFILE_PHY_CTRL,
	.rfile_phy_tsel = MARVELL_RFILE_PHY_TSEL,
	.rfile_phy_dq_timing = MARVELL_RFILE_PHY_DQ_TIMING,
	.rfile_phy_dqs_timing = MARVELL_RFILE_PHY_DQS_TIMING,
	.rfile_phy_gate_lpbk_ctrl = MARVELL_RFILE_PHY_GATE_LPBK_CTRL,
	.rfile_phy_dll_master_ctrl = MARVELL_RFILE_PHY_DLL_MASTER_CTRL,
	.rfile_phy_dll_slave_ctrl = MARVELL_RFILE_PHY_DLL_SLAVE_CTRL,
};

static struct cdns_xspi_driver_data cdns_driver_data = {
	.mrvl_hw_overlay = false,
};

static const int cdns_mrvl_xspi_clk_div_list[] = {
	4,	//0x0 = Divide by 4.   SPI clock is 200 MHz.
	6,	//0x1 = Divide by 6.   SPI clock is 133.33 MHz.
	8,	//0x2 = Divide by 8.   SPI clock is 100 MHz.
	10,	//0x3 = Divide by 10.  SPI clock is 80 MHz.
	12,	//0x4 = Divide by 12.  SPI clock is 66.666 MHz.
	16,	//0x5 = Divide by 16.  SPI clock is 50 MHz.
	18,	//0x6 = Divide by 18.  SPI clock is 44.44 MHz.
	20,	//0x7 = Divide by 20.  SPI clock is 40 MHz.
	24,	//0x8 = Divide by 24.  SPI clock is 33.33 MHz.
	32,	//0x9 = Divide by 32.  SPI clock is 25 MHz.
	40,	//0xA = Divide by 40.  SPI clock is 20 MHz.
	50,	//0xB = Divide by 50.  SPI clock is 16 MHz.
	64,	//0xC = Divide by 64.  SPI clock is 12.5 MHz.
	128	//0xD = Divide by 128. SPI clock is 6.25 MHz.
};

struct cdns_xspi_dev {
	struct platform_device *pdev;
	struct device *dev;

	void __iomem *iobase;
	void __iomem *auxbase;
	void __iomem *sdmabase;
	void __iomem *xferbase;

	int irq;
	int cur_cs;
	unsigned int sdmasize;

	struct completion cmd_complete;
	struct completion auto_cmd_complete;
	struct completion sdma_complete;
	bool sdma_error;

	void *in_buffer;
	const void *out_buffer;

	u8 hw_num_banks;

	const struct cdns_xspi_driver_data *driver_data;
	void (*sdma_handler)(struct cdns_xspi_dev *cdns_xspi);
	void (*set_interrupts_handler)(struct cdns_xspi_dev *cdns_xspi, bool enabled);

	bool xfer_in_progress;
	int current_xfer_qword;
};

static void cdns_xspi_reset_dll(struct cdns_xspi_dev *cdns_xspi)
{
	u32 dll_cntrl = readl(cdns_xspi->iobase +
			      CDNS_XSPI_RF_MINICTRL_REGS_DLL_PHY_CTRL);

	/* Reset DLL */
	dll_cntrl |= CDNS_XSPI_DLL_RST_N;
	writel(dll_cntrl, cdns_xspi->iobase +
			  CDNS_XSPI_RF_MINICTRL_REGS_DLL_PHY_CTRL);
}

static bool cdns_xspi_is_dll_locked(struct cdns_xspi_dev *cdns_xspi)
{
	u32 dll_lock;

	return !readl_relaxed_poll_timeout(cdns_xspi->iobase +
		CDNS_XSPI_INTR_STATUS_REG,
		dll_lock, ((dll_lock & CDNS_XSPI_DLL_LOCK) == 1), 10, 10000);
}

/* Static configuration of PHY */
static bool cdns_xspi_configure_phy(struct cdns_xspi_dev *cdns_xspi)
{
	writel(cdns_xspi->driver_data->dll_phy_ctrl,
	       cdns_xspi->iobase + CDNS_XSPI_RF_MINICTRL_REGS_DLL_PHY_CTRL);
	writel(cdns_xspi->driver_data->ctb_rfile_phy_ctrl,
	       cdns_xspi->auxbase + CDNS_XSPI_PHY_CTB_RFILE_PHY_CTRL);
	writel(cdns_xspi->driver_data->rfile_phy_tsel,
	       cdns_xspi->auxbase + CDNS_XSPI_PHY_CTB_RFILE_PHY_TSEL);
	writel(cdns_xspi->driver_data->rfile_phy_dq_timing,
	       cdns_xspi->auxbase + CDNS_XSPI_PHY_DATASLICE_RFILE_PHY_DQ_TIMING);
	writel(cdns_xspi->driver_data->rfile_phy_dqs_timing,
	       cdns_xspi->auxbase + CDNS_XSPI_PHY_DATASLICE_RFILE_PHY_DQS_TIMING);
	writel(cdns_xspi->driver_data->rfile_phy_gate_lpbk_ctrl,
	       cdns_xspi->auxbase + CDNS_XSPI_PHY_DATASLICE_RFILE_PHY_GATE_LPBK_CTRL);
	writel(cdns_xspi->driver_data->rfile_phy_dll_master_ctrl,
	       cdns_xspi->auxbase + CDNS_XSPI_PHY_DATASLICE_RFILE_PHY_DLL_MASTER_CTRL);
	writel(cdns_xspi->driver_data->rfile_phy_dll_slave_ctrl,
	       cdns_xspi->auxbase + CDNS_XSPI_PHY_DATASLICE_RFILE_PHY_DLL_SLAVE_CTRL);

	cdns_xspi_reset_dll(cdns_xspi);

	return cdns_xspi_is_dll_locked(cdns_xspi);
}

static bool cdns_mrvl_xspi_setup_clock(struct cdns_xspi_dev *cdns_xspi,
				       int requested_clk)
{
	int i = 0;
	int clk_val;
	u32 clk_reg;
	bool update_clk = false;

	while (i < ARRAY_SIZE(cdns_mrvl_xspi_clk_div_list)) {
		clk_val = MRVL_XSPI_CLOCK_DIVIDED(
				cdns_mrvl_xspi_clk_div_list[i]);
		if (clk_val <= requested_clk)
			break;
		i++;
	}

	dev_dbg(cdns_xspi->dev, "Found clk div: %d, clk val: %d\n",
		cdns_mrvl_xspi_clk_div_list[i],
		MRVL_XSPI_CLOCK_DIVIDED(
		cdns_mrvl_xspi_clk_div_list[i]));

	clk_reg = readl(cdns_xspi->auxbase + MRVL_XSPI_CLK_CTRL_AUX_REG);

	if (FIELD_GET(MRVL_XSPI_CLK_DIV, clk_reg) != i) {
		clk_reg &= ~MRVL_XSPI_CLK_ENABLE;
		writel(clk_reg,
		       cdns_xspi->auxbase + MRVL_XSPI_CLK_CTRL_AUX_REG);
		clk_reg = FIELD_PREP(MRVL_XSPI_CLK_DIV, i);
		clk_reg &= ~MRVL_XSPI_CLK_DIV;
		clk_reg |= FIELD_PREP(MRVL_XSPI_CLK_DIV, i);
		clk_reg |= MRVL_XSPI_CLK_ENABLE;
		clk_reg |= MRVL_XSPI_IRQ_ENABLE;
		update_clk = true;
	}

	if (update_clk)
		writel(clk_reg,
		       cdns_xspi->auxbase + MRVL_XSPI_CLK_CTRL_AUX_REG);

	return update_clk;
}

static int cdns_xspi_wait_for_controller_idle(struct cdns_xspi_dev *cdns_xspi)
{
	u32 ctrl_stat;

	return readl_relaxed_poll_timeout(cdns_xspi->iobase +
					  CDNS_XSPI_CTRL_STATUS_REG,
					  ctrl_stat,
					  ((ctrl_stat &
					    CDNS_XSPI_CTRL_BUSY) == 0),
					  100, 1000);
}

static void cdns_xspi_trigger_command(struct cdns_xspi_dev *cdns_xspi,
				      u32 cmd_regs[6])
{
	writel(cmd_regs[5], cdns_xspi->iobase + CDNS_XSPI_CMD_REG_5);
	writel(cmd_regs[4], cdns_xspi->iobase + CDNS_XSPI_CMD_REG_4);
	writel(cmd_regs[3], cdns_xspi->iobase + CDNS_XSPI_CMD_REG_3);
	writel(cmd_regs[2], cdns_xspi->iobase + CDNS_XSPI_CMD_REG_2);
	writel(cmd_regs[1], cdns_xspi->iobase + CDNS_XSPI_CMD_REG_1);
	writel(cmd_regs[0], cdns_xspi->iobase + CDNS_XSPI_CMD_REG_0);
}

static int cdns_xspi_check_command_status(struct cdns_xspi_dev *cdns_xspi)
{
	int ret = 0;
	u32 cmd_status = readl(cdns_xspi->iobase + CDNS_XSPI_CMD_STATUS_REG);

	if (cmd_status & CDNS_XSPI_CMD_STATUS_COMPLETED) {
		if ((cmd_status & CDNS_XSPI_CMD_STATUS_FAILED) != 0) {
			if (cmd_status & CDNS_XSPI_CMD_STATUS_DQS_ERROR) {
				dev_err(cdns_xspi->dev,
					"Incorrect DQS pulses detected\n");
				ret = -EPROTO;
			}
			if (cmd_status & CDNS_XSPI_CMD_STATUS_CRC_ERROR) {
				dev_err(cdns_xspi->dev,
					"CRC error received\n");
				ret = -EPROTO;
			}
			if (cmd_status & CDNS_XSPI_CMD_STATUS_BUS_ERROR) {
				dev_err(cdns_xspi->dev,
					"Error resp on system DMA interface\n");
				ret = -EPROTO;
			}
			if (cmd_status & CDNS_XSPI_CMD_STATUS_INV_SEQ_ERROR) {
				dev_err(cdns_xspi->dev,
					"Invalid command sequence detected\n");
				ret = -EPROTO;
			}
		}
	} else {
		dev_err(cdns_xspi->dev, "Fatal err - command not completed\n");
		ret = -EPROTO;
	}

	return ret;
}

static void cdns_xspi_set_interrupts(struct cdns_xspi_dev *cdns_xspi,
				     bool enabled)
{
	u32 intr_enable;

	intr_enable = readl(cdns_xspi->iobase + CDNS_XSPI_INTR_ENABLE_REG);
	if (enabled)
		intr_enable |= CDNS_XSPI_INTR_MASK;
	else
		intr_enable &= ~CDNS_XSPI_INTR_MASK;
	writel(intr_enable, cdns_xspi->iobase + CDNS_XSPI_INTR_ENABLE_REG);
}

static void marvell_xspi_set_interrupts(struct cdns_xspi_dev *cdns_xspi,
				     bool enabled)
{
	u32 intr_enable;
	u32 irq_status;

	irq_status = readl(cdns_xspi->iobase + CDNS_XSPI_INTR_STATUS_REG);
	writel(irq_status, cdns_xspi->iobase + CDNS_XSPI_INTR_STATUS_REG);

	intr_enable = readl(cdns_xspi->iobase + CDNS_XSPI_INTR_ENABLE_REG);
	if (enabled)
		intr_enable |= CDNS_XSPI_INTR_MASK;
	else
		intr_enable &= ~CDNS_XSPI_INTR_MASK;
	writel(intr_enable, cdns_xspi->iobase + CDNS_XSPI_INTR_ENABLE_REG);
}

static int cdns_xspi_controller_init(struct cdns_xspi_dev *cdns_xspi)
{
	u32 ctrl_ver;
	u32 ctrl_features;
	u16 hw_magic_num;

	ctrl_ver = readl(cdns_xspi->iobase + CDNS_XSPI_CTRL_VERSION_REG);
	hw_magic_num = FIELD_GET(CDNS_XSPI_MAGIC_NUM, ctrl_ver);
	if (hw_magic_num != CDNS_XSPI_MAGIC_NUM_VALUE) {
		dev_err(cdns_xspi->dev,
			"Incorrect XSPI magic number: %x, expected: %x\n",
			hw_magic_num, CDNS_XSPI_MAGIC_NUM_VALUE);
		return -EIO;
	}

	ctrl_features = readl(cdns_xspi->iobase + CDNS_XSPI_CTRL_FEATURES_REG);
	cdns_xspi->hw_num_banks = FIELD_GET(CDNS_XSPI_NUM_BANKS, ctrl_features);
	cdns_xspi->set_interrupts_handler(cdns_xspi, false);

	return 0;
}

static void cdns_xspi_sdma_handle(struct cdns_xspi_dev *cdns_xspi)
{
	u32 sdma_size, sdma_trd_info;
	u8 sdma_dir;

	sdma_size = readl(cdns_xspi->iobase + CDNS_XSPI_SDMA_SIZE_REG);
	sdma_trd_info = readl(cdns_xspi->iobase + CDNS_XSPI_SDMA_TRD_INFO_REG);
	sdma_dir = FIELD_GET(CDNS_XSPI_SDMA_DIR, sdma_trd_info);

	switch (sdma_dir) {
	case CDNS_XSPI_SDMA_DIR_READ:
		ioread8_rep(cdns_xspi->sdmabase,
			    cdns_xspi->in_buffer, sdma_size);
		break;

	case CDNS_XSPI_SDMA_DIR_WRITE:
		iowrite8_rep(cdns_xspi->sdmabase,
			     cdns_xspi->out_buffer, sdma_size);
		break;
	}
}

static void m_ioreadq(void __iomem  *addr, void *buf, int len)
{
	if (IS_ALIGNED((long)buf, 8) && len >= 8) {
		u64 full_ops = len / 8;
		u64 *buffer = buf;

		len -= full_ops * 8;
		buf += full_ops * 8;

		do {
			u64 b = readq(addr);
			*buffer++ = b;
		} while (--full_ops);
	}


	while (len) {
		u64 tmp_buf;

		tmp_buf = readq(addr);
		memcpy(buf, &tmp_buf, min(len, 8));
		len = len > 8 ? len - 8 : 0;
		buf += 8;
	}
}

static void m_iowriteq(void __iomem *addr, const void *buf, int len)
{
	if (IS_ALIGNED((long)buf, 8) && len >= 8) {
		u64 full_ops = len / 8;
		const u64 *buffer = buf;

		len -= full_ops * 8;
		buf += full_ops * 8;

		do {
			writeq(*buffer++, addr);
		} while (--full_ops);
	}

	while (len) {
		u64 tmp_buf;

		memcpy(&tmp_buf, buf, min(len, 8));
		writeq(tmp_buf, addr);
		len = len > 8 ? len - 8 : 0;
		buf += 8;
	}
}

static void marvell_xspi_sdma_handle(struct cdns_xspi_dev *cdns_xspi)
{
	u32 sdma_size, sdma_trd_info;
	u8 sdma_dir;

	sdma_size = readl(cdns_xspi->iobase + CDNS_XSPI_SDMA_SIZE_REG);
	sdma_trd_info = readl(cdns_xspi->iobase + CDNS_XSPI_SDMA_TRD_INFO_REG);
	sdma_dir = FIELD_GET(CDNS_XSPI_SDMA_DIR, sdma_trd_info);

	switch (sdma_dir) {
	case CDNS_XSPI_SDMA_DIR_READ:
		m_ioreadq(cdns_xspi->sdmabase,
			    cdns_xspi->in_buffer, sdma_size);
		break;

	case CDNS_XSPI_SDMA_DIR_WRITE:
		m_iowriteq(cdns_xspi->sdmabase,
			     cdns_xspi->out_buffer, sdma_size);
		break;
	}
}

static int cdns_xspi_send_stig_command(struct cdns_xspi_dev *cdns_xspi,
				       const struct spi_mem_op *op,
				       bool data_phase)
{
	u32 cmd_regs[6];
	u32 cmd_status;
	int ret;
	int dummybytes = op->dummy.nbytes;

	ret = cdns_xspi_wait_for_controller_idle(cdns_xspi);
	if (ret < 0)
		return -EIO;

	writel(FIELD_PREP(CDNS_XSPI_CTRL_WORK_MODE, CDNS_XSPI_WORK_MODE_STIG),
	       cdns_xspi->iobase + CDNS_XSPI_CTRL_CONFIG_REG);

	cdns_xspi->set_interrupts_handler(cdns_xspi, true);
	cdns_xspi->sdma_error = false;

	memset(cmd_regs, 0, sizeof(cmd_regs));
	cmd_regs[1] = CDNS_XSPI_CMD_FLD_P1_INSTR_CMD_1(op, data_phase);
	cmd_regs[2] = CDNS_XSPI_CMD_FLD_P1_INSTR_CMD_2(op);
	if (dummybytes != 0) {
		cmd_regs[3] = CDNS_XSPI_CMD_FLD_P1_INSTR_CMD_3(op, 1);
		dummybytes--;
	} else {
		cmd_regs[3] = CDNS_XSPI_CMD_FLD_P1_INSTR_CMD_3(op, 0);
	}
	cmd_regs[4] = CDNS_XSPI_CMD_FLD_P1_INSTR_CMD_4(op,
						       cdns_xspi->cur_cs);

	cdns_xspi_trigger_command(cdns_xspi, cmd_regs);

	if (data_phase) {
		cmd_regs[0] = CDNS_XSPI_STIG_DONE_FLAG;
		cmd_regs[1] = CDNS_XSPI_CMD_FLD_DSEQ_CMD_1(op);
		cmd_regs[2] = CDNS_XSPI_CMD_FLD_DSEQ_CMD_2(op);
		cmd_regs[3] = CDNS_XSPI_CMD_FLD_DSEQ_CMD_3(op, dummybytes);
		cmd_regs[4] = CDNS_XSPI_CMD_FLD_DSEQ_CMD_4(op,
							   cdns_xspi->cur_cs);

		cdns_xspi->in_buffer = op->data.buf.in;
		cdns_xspi->out_buffer = op->data.buf.out;

		cdns_xspi_trigger_command(cdns_xspi, cmd_regs);

		wait_for_completion(&cdns_xspi->sdma_complete);
		if (cdns_xspi->sdma_error) {
			cdns_xspi->set_interrupts_handler(cdns_xspi, false);
			return -EIO;
		}
		cdns_xspi->sdma_handler(cdns_xspi);
	}

	wait_for_completion(&cdns_xspi->cmd_complete);
	cdns_xspi->set_interrupts_handler(cdns_xspi, false);

	cmd_status = cdns_xspi_check_command_status(cdns_xspi);
	if (cmd_status)
		return -EPROTO;

	return 0;
}

static int cdns_xspi_mem_op(struct cdns_xspi_dev *cdns_xspi,
			    struct spi_mem *mem,
			    const struct spi_mem_op *op)
{
	enum spi_mem_data_dir dir = op->data.dir;

	if (cdns_xspi->cur_cs != spi_get_chipselect(mem->spi, 0))
		cdns_xspi->cur_cs = spi_get_chipselect(mem->spi, 0);

	return cdns_xspi_send_stig_command(cdns_xspi, op,
					   (dir != SPI_MEM_NO_DATA));
}

static int cdns_xspi_mem_op_execute(struct spi_mem *mem,
				    const struct spi_mem_op *op)
{
	struct cdns_xspi_dev *cdns_xspi =
		spi_controller_get_devdata(mem->spi->controller);
	int ret = 0;

	ret = cdns_xspi_mem_op(cdns_xspi, mem, op);

	return ret;
}

static int marvell_xspi_mem_op_execute(struct spi_mem *mem,
				    const struct spi_mem_op *op)
{
	struct cdns_xspi_dev *cdns_xspi =
		spi_controller_get_devdata(mem->spi->controller);
	int ret = 0;

	cdns_mrvl_xspi_setup_clock(cdns_xspi, mem->spi->max_speed_hz);

	ret = cdns_xspi_mem_op(cdns_xspi, mem, op);

	return ret;
}

#ifdef CONFIG_ACPI
static bool cdns_xspi_supports_op(struct spi_mem *mem,
				  const struct spi_mem_op *op)
{
	struct spi_device *spi = mem->spi;
	const union acpi_object *obj;
	struct acpi_device *adev;

	adev = ACPI_COMPANION(&spi->dev);

	if (!acpi_dev_get_property(adev, "spi-tx-bus-width", ACPI_TYPE_INTEGER,
				   &obj)) {
		switch (obj->integer.value) {
		case 1:
			break;
		case 2:
			spi->mode |= SPI_TX_DUAL;
			break;
		case 4:
			spi->mode |= SPI_TX_QUAD;
			break;
		case 8:
			spi->mode |= SPI_TX_OCTAL;
			break;
		default:
			dev_warn(&spi->dev,
				 "spi-tx-bus-width %lld not supported\n",
				 obj->integer.value);
			break;
		}
	}

	if (!acpi_dev_get_property(adev, "spi-rx-bus-width", ACPI_TYPE_INTEGER,
				   &obj)) {
		switch (obj->integer.value) {
		case 1:
			break;
		case 2:
			spi->mode |= SPI_RX_DUAL;
			break;
		case 4:
			spi->mode |= SPI_RX_QUAD;
			break;
		case 8:
			spi->mode |= SPI_RX_OCTAL;
			break;
		default:
			dev_warn(&spi->dev,
				 "spi-rx-bus-width %lld not supported\n",
				 obj->integer.value);
			break;
		}
	}

	if (!spi_mem_default_supports_op(mem, op))
		return false;

	return true;
}
#endif

static int cdns_xspi_adjust_mem_op_size(struct spi_mem *mem, struct spi_mem_op *op)
{
	struct cdns_xspi_dev *cdns_xspi =
		spi_controller_get_devdata(mem->spi->controller);

	op->data.nbytes = clamp_val(op->data.nbytes, 0, cdns_xspi->sdmasize);

	return 0;
}

static const struct spi_controller_mem_ops cadence_xspi_mem_ops = {
#ifdef CONFIG_ACPI
	.supports_op = cdns_xspi_supports_op,
#endif
	.exec_op = cdns_xspi_mem_op_execute,
	.adjust_op_size = cdns_xspi_adjust_mem_op_size,
};

static const struct spi_controller_mem_ops marvell_xspi_mem_ops = {
#ifdef CONFIG_ACPI
	.supports_op = cdns_xspi_supports_op,
#endif
	.exec_op = marvell_xspi_mem_op_execute,
	.adjust_op_size = cdns_xspi_adjust_mem_op_size,
};

static irqreturn_t cdns_xspi_irq_handler(int this_irq, void *dev)
{
	struct cdns_xspi_dev *cdns_xspi = dev;
	u32 irq_status;
	irqreturn_t result = IRQ_NONE;

	irq_status = readl(cdns_xspi->iobase + CDNS_XSPI_INTR_STATUS_REG);
	writel(irq_status, cdns_xspi->iobase + CDNS_XSPI_INTR_STATUS_REG);

	if (irq_status &
	    (CDNS_XSPI_SDMA_ERROR | CDNS_XSPI_SDMA_TRIGGER |
	     CDNS_XSPI_STIG_DONE)) {
		if (irq_status & CDNS_XSPI_SDMA_ERROR) {
			dev_err(cdns_xspi->dev,
				"Slave DMA transaction error\n");
			cdns_xspi->sdma_error = true;
			complete(&cdns_xspi->sdma_complete);
		}

		if (irq_status & CDNS_XSPI_SDMA_TRIGGER)
			complete(&cdns_xspi->sdma_complete);

		if (irq_status & CDNS_XSPI_STIG_DONE)
			complete(&cdns_xspi->cmd_complete);

		result = IRQ_HANDLED;
	}

	irq_status = readl(cdns_xspi->iobase + CDNS_XSPI_TRD_COMP_INTR_STATUS);
	if (irq_status) {
		writel(irq_status,
		       cdns_xspi->iobase + CDNS_XSPI_TRD_COMP_INTR_STATUS);

		complete(&cdns_xspi->auto_cmd_complete);

		result = IRQ_HANDLED;
	}

	return result;
}

static int cdns_xspi_of_get_plat_data(struct platform_device *pdev)
{
	struct fwnode_handle *fwnode_child;
	unsigned int cs;

	device_for_each_child_node(&pdev->dev, fwnode_child) {
		if (!fwnode_device_is_available(fwnode_child))
			continue;

		if (fwnode_property_read_u32(fwnode_child, "reg", &cs)) {
			dev_err(&pdev->dev, "Couldn't get memory chip select\n");
			fwnode_handle_put(fwnode_child);
			return -ENXIO;
		} else if (cs >= CDNS_XSPI_MAX_BANKS) {
			dev_err(&pdev->dev, "reg (cs) parameter value too large\n");
			fwnode_handle_put(fwnode_child);
			return -ENXIO;
		}
	}

	return 0;
}

static void cdns_xspi_print_phy_config(struct cdns_xspi_dev *cdns_xspi)
{
	struct device *dev = cdns_xspi->dev;

	dev_info(dev, "PHY configuration\n");
	dev_info(dev, "   * xspi_dll_phy_ctrl: %08x\n",
		 readl(cdns_xspi->iobase + CDNS_XSPI_DLL_PHY_CTRL));
	dev_info(dev, "   * phy_dq_timing: %08x\n",
		 readl(cdns_xspi->auxbase + CDNS_XSPI_CCP_PHY_DQ_TIMING));
	dev_info(dev, "   * phy_dqs_timing: %08x\n",
		 readl(cdns_xspi->auxbase + CDNS_XSPI_CCP_PHY_DQS_TIMING));
	dev_info(dev, "   * phy_gate_loopback_ctrl: %08x\n",
		 readl(cdns_xspi->auxbase + CDNS_XSPI_CCP_PHY_GATE_LPBCK_CTRL));
	dev_info(dev, "   * phy_dll_slave_ctrl: %08x\n",
		 readl(cdns_xspi->auxbase + CDNS_XSPI_CCP_PHY_DLL_SLAVE_CTRL));
}

static int cdns_xspi_prepare_generic(int cs, const void *dout, int len, int glue, u32 *cmd_regs)
{
	u8 *data = (u8 *)dout;
	int i;
	int data_counter = 0;

	memset(cmd_regs, 0x00, CMD_REG_LEN);

	if (GENERIC_CMD_REG_3_NEEDED(len)) {
		for (i = GENERIC_CMD_DATA_REG_3_COUNT(len); i >= 0 ; i--)
			cmd_regs[3] |= GENERIC_CMD_DATA_INSERT(data[data_counter++],
							       GENERIC_CMD_DATA_3_OFFSET(i));
	}
	if (GENERIC_CMD_REG_2_NEEDED(len)) {
		for (i = GENERIC_CMD_DATA_REG_2_COUNT(len); i >= 0; i--)
			cmd_regs[2] |= GENERIC_CMD_DATA_INSERT(data[data_counter++],
							       GENERIC_CMD_DATA_2_OFFSET(i));
	}
	for (i = GENERIC_CMD_DATA_REG_1_COUNT(len); i >= 0 ; i--)
		cmd_regs[1] |= GENERIC_CMD_DATA_INSERT(data[data_counter++],
						       GENERIC_CMD_DATA_1_OFFSET(i));

	cmd_regs[1] |= CDNS_XSPI_CMD_FLD_P1_GENERIC_CMD;
	cmd_regs[3] |= CDNS_XSPI_CMD_FLD_P3_GENERIC_CMD(len);
	cmd_regs[4] |= CDNS_XSPI_CMD_FLD_P4_GENERIC_CMD(cs, glue);

	return 0;
}

static void marvell_xspi_read_single_qword(struct cdns_xspi_dev *cdns_xspi, u8 **buffer)
{
	u64 d = readq(cdns_xspi->xferbase +
		      MRVL_XFER_FUNC_CTRL_READ_DATA(cdns_xspi->current_xfer_qword));
	u8 *ptr = (u8 *)&d;
	int k;

	for (k = 0; k < 8; k++) {
		u8 val = bitrev8((ptr[k]));
		**buffer = val;
		*buffer = *buffer + 1;
	}

	cdns_xspi->current_xfer_qword++;
	cdns_xspi->current_xfer_qword %= MRVL_XFER_QWORD_COUNT;
}

static void cdns_xspi_finish_read(struct cdns_xspi_dev *cdns_xspi, u8 **buffer, u32 data_count)
{
	u64 d = readq(cdns_xspi->xferbase +
		      MRVL_XFER_FUNC_CTRL_READ_DATA(cdns_xspi->current_xfer_qword));
	u8 *ptr = (u8 *)&d;
	int k;

	for (k = 0; k < data_count % MRVL_XFER_QWORD_BYTECOUNT; k++) {
		u8 val = bitrev8((ptr[k]));
		**buffer = val;
		*buffer = *buffer + 1;
	}

	cdns_xspi->current_xfer_qword++;
	cdns_xspi->current_xfer_qword %= MRVL_XFER_QWORD_COUNT;
}

static int cdns_xspi_prepare_transfer(int cs, int dir, int len, u32 *cmd_regs)
{
	memset(cmd_regs, 0x00, CMD_REG_LEN);

	cmd_regs[1] |= CDNS_XSPI_CMD_FLD_GENERIC_DSEQ_CMD_1;
	cmd_regs[2] |= CDNS_XSPI_CMD_FLD_GENERIC_DSEQ_CMD_2(len);
	cmd_regs[4] |= CDNS_XSPI_CMD_FLD_GENERIC_DSEQ_CMD_4(dir, cs);

	return 0;
}

static bool cdns_xspi_is_stig_ready(struct cdns_xspi_dev *cdns_xspi, bool sleep)
{
	u32 ctrl_stat;

	return !readl_relaxed_poll_timeout
		(cdns_xspi->iobase + CDNS_XSPI_CTRL_STATUS_REG,
		ctrl_stat,
		((ctrl_stat & BIT(3)) == 0),
		sleep ? MRVL_XSPI_POLL_DELAY_US : 0,
		sleep ? MRVL_XSPI_POLL_TIMEOUT_US : 0);
}

static bool cdns_xspi_is_sdma_ready(struct cdns_xspi_dev *cdns_xspi, bool sleep)
{
	u32 ctrl_stat;

	return !readl_relaxed_poll_timeout
		(cdns_xspi->iobase + CDNS_XSPI_INTR_STATUS_REG,
		ctrl_stat,
		(ctrl_stat & CDNS_XSPI_SDMA_TRIGGER),
		sleep ? MRVL_XSPI_POLL_DELAY_US : 0,
		sleep ? MRVL_XSPI_POLL_TIMEOUT_US : 0);
}

static int cdns_xspi_transfer_one_message_b0(struct spi_controller *controller,
					   struct spi_message *m)
{
	struct cdns_xspi_dev *cdns_xspi = spi_controller_get_devdata(controller);
	struct spi_device *spi = m->spi;
	struct spi_transfer *t = NULL;

	const unsigned int max_len = MRVL_XFER_QWORD_BYTECOUNT * MRVL_XFER_QWORD_COUNT;
	int current_transfer_len;
	int cs = spi_get_chipselect(spi, 0);
	int cs_change = 0;

	/* Enable xfer state machine */
	if (!cdns_xspi->xfer_in_progress) {
		u32 xfer_control = readl(cdns_xspi->xferbase + MRVL_XFER_FUNC_CTRL);

		cdns_xspi->current_xfer_qword = 0;
		cdns_xspi->xfer_in_progress = true;
		xfer_control |= (MRVL_XFER_RECEIVE_ENABLE |
				 MRVL_XFER_CLK_CAPTURE_POL |
				 MRVL_XFER_FUNC_START |
				 MRVL_XFER_SOFT_RESET |
				 FIELD_PREP(MRVL_XFER_CS_N_HOLD, (1 << cs)));
		xfer_control &= ~(MRVL_XFER_FUNC_ENABLE | MRVL_XFER_CLK_DRIVE_POL);
		writel(xfer_control, cdns_xspi->xferbase + MRVL_XFER_FUNC_CTRL);
	}

	list_for_each_entry(t, &m->transfers, transfer_list) {
		u8 *txd = (u8 *) t->tx_buf;
		u8 *rxd = (u8 *) t->rx_buf;
		u8 data[10];
		u32 cmd_regs[6];

		if (!txd)
			txd = data;

		cdns_xspi->in_buffer = txd + 1;
		cdns_xspi->out_buffer = txd + 1;

		while (t->len) {

			current_transfer_len = min(max_len, t->len);

			if (current_transfer_len < 10) {
				cdns_xspi_prepare_generic(cs, txd, current_transfer_len,
							  false, cmd_regs);
				cdns_xspi_trigger_command(cdns_xspi, cmd_regs);
				if (!cdns_xspi_is_stig_ready(cdns_xspi, true))
					return -EIO;
			} else {
				cdns_xspi_prepare_generic(cs, txd, 1, true, cmd_regs);
				cdns_xspi_trigger_command(cdns_xspi, cmd_regs);
				cdns_xspi_prepare_transfer(cs, 1, current_transfer_len - 1,
							   cmd_regs);
				cdns_xspi_trigger_command(cdns_xspi, cmd_regs);
				if (!cdns_xspi_is_sdma_ready(cdns_xspi, true))
					return -EIO;
				cdns_xspi->sdma_handler(cdns_xspi);
				if (!cdns_xspi_is_stig_ready(cdns_xspi, true))
					return -EIO;

				cdns_xspi->in_buffer += current_transfer_len;
				cdns_xspi->out_buffer += current_transfer_len;
			}

			if (rxd) {
				int j;

				for (j = 0; j < current_transfer_len / 8; j++)
					marvell_xspi_read_single_qword(cdns_xspi, &rxd);
				cdns_xspi_finish_read(cdns_xspi, &rxd, current_transfer_len);
			} else {
				cdns_xspi->current_xfer_qword += current_transfer_len /
								 MRVL_XFER_QWORD_BYTECOUNT;
				if (current_transfer_len % MRVL_XFER_QWORD_BYTECOUNT)
					cdns_xspi->current_xfer_qword++;

				cdns_xspi->current_xfer_qword %= MRVL_XFER_QWORD_COUNT;
			}
			cs_change = t->cs_change;
			t->len -= current_transfer_len;
		}
		spi_transfer_delay_exec(t);
	}

	if (!cs_change) {
		u32 xfer_control = readl(cdns_xspi->xferbase + MRVL_XFER_FUNC_CTRL);

		xfer_control &= ~(MRVL_XFER_RECEIVE_ENABLE |
				  MRVL_XFER_SOFT_RESET);
		writel(xfer_control, cdns_xspi->xferbase + MRVL_XFER_FUNC_CTRL);
		cdns_xspi->xfer_in_progress = false;
	}

	m->status = 0;
	spi_finalize_current_message(controller);

	return 0;
}

static int cdns_xspi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct spi_controller *host = NULL;
	struct cdns_xspi_dev *cdns_xspi = NULL;
	struct resource *res;
	int ret;

	host = devm_spi_alloc_host(dev, sizeof(*cdns_xspi));
	if (!host)
		return -ENOMEM;

	host->mode_bits = SPI_3WIRE | SPI_TX_DUAL  | SPI_TX_QUAD  |
		SPI_RX_DUAL | SPI_RX_QUAD | SPI_TX_OCTAL | SPI_RX_OCTAL |
		SPI_MODE_0  | SPI_MODE_3;

	cdns_xspi = spi_controller_get_devdata(host);
	cdns_xspi->driver_data = of_device_get_match_data(dev);
	if (!cdns_xspi->driver_data) {
		cdns_xspi->driver_data = acpi_device_get_match_data(dev);
		if (!cdns_xspi->driver_data)
			return -ENODEV;
	}

	if (cdns_xspi->driver_data->mrvl_hw_overlay) {
		host->mem_ops = &marvell_xspi_mem_ops;
		host->transfer_one_message = cdns_xspi_transfer_one_message_b0;
		cdns_xspi->sdma_handler = &marvell_xspi_sdma_handle;
		cdns_xspi->set_interrupts_handler = &marvell_xspi_set_interrupts;
	} else {
		host->mem_ops = &cadence_xspi_mem_ops;
		cdns_xspi->sdma_handler = &cdns_xspi_sdma_handle;
		cdns_xspi->set_interrupts_handler = &cdns_xspi_set_interrupts;
	}
	host->dev.of_node = pdev->dev.of_node;
	host->bus_num = -1;

	platform_set_drvdata(pdev, host);

	cdns_xspi->pdev = pdev;
	cdns_xspi->dev = &pdev->dev;
	cdns_xspi->cur_cs = 0;

	init_completion(&cdns_xspi->cmd_complete);
	init_completion(&cdns_xspi->auto_cmd_complete);
	init_completion(&cdns_xspi->sdma_complete);

	ret = cdns_xspi_of_get_plat_data(pdev);
	if (ret)
		return -ENODEV;

	cdns_xspi->iobase = devm_platform_ioremap_resource_byname(pdev, "io");
	if (IS_ERR(cdns_xspi->iobase)) {
		cdns_xspi->iobase = devm_platform_ioremap_resource(pdev, 0);
		if (IS_ERR(cdns_xspi->iobase)) {
			dev_err(dev, "Failed to remap controller base address\n");
			return PTR_ERR(cdns_xspi->iobase);
		}
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "sdma");
	cdns_xspi->sdmabase = devm_ioremap_resource(dev, res);
	if (IS_ERR(cdns_xspi->sdmabase)) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		cdns_xspi->sdmabase = devm_ioremap_resource(dev, res);
		if (IS_ERR(cdns_xspi->sdmabase))
			return PTR_ERR(cdns_xspi->sdmabase);
	}
	cdns_xspi->sdmasize = resource_size(res);

	cdns_xspi->auxbase = devm_platform_ioremap_resource_byname(pdev, "aux");
	if (IS_ERR(cdns_xspi->auxbase)) {
		cdns_xspi->auxbase = devm_platform_ioremap_resource(pdev, 2);
		if (IS_ERR(cdns_xspi->auxbase)) {
			dev_err(dev, "Failed to remap AUX address\n");
			return PTR_ERR(cdns_xspi->auxbase);
		}
	}

	if (cdns_xspi->driver_data->mrvl_hw_overlay) {
		cdns_xspi->xferbase = devm_platform_ioremap_resource_byname(pdev, "xfer");
		if (IS_ERR(cdns_xspi->xferbase)) {
			cdns_xspi->xferbase = devm_platform_ioremap_resource(pdev, 3);
			if (IS_ERR(cdns_xspi->xferbase)) {
				dev_info(dev, "XFER register base not found, set it\n");
				// For compatibility with older firmware
				cdns_xspi->xferbase = cdns_xspi->iobase + 0x8000;
			}
		}
	}

	cdns_xspi->irq = platform_get_irq(pdev, 0);
	if (cdns_xspi->irq < 0)
		return -ENXIO;

	ret = devm_request_irq(dev, cdns_xspi->irq, cdns_xspi_irq_handler,
			       IRQF_SHARED, pdev->name, cdns_xspi);
	if (ret) {
		dev_err(dev, "Failed to request IRQ: %d\n", cdns_xspi->irq);
		return ret;
	}

	if (cdns_xspi->driver_data->mrvl_hw_overlay) {
		cdns_mrvl_xspi_setup_clock(cdns_xspi, MRVL_DEFAULT_CLK);
		cdns_xspi_configure_phy(cdns_xspi);
	}

	cdns_xspi_print_phy_config(cdns_xspi);

	ret = cdns_xspi_controller_init(cdns_xspi);
	if (ret) {
		dev_err(dev, "Failed to initialize controller\n");
		return ret;
	}

	host->num_chipselect = 1 << cdns_xspi->hw_num_banks;

	ret = devm_spi_register_controller(dev, host);
	if (ret) {
		dev_err(dev, "Failed to register SPI host\n");
		return ret;
	}

	dev_info(dev, "Successfully registered SPI host\n");

	return 0;
}

static const struct of_device_id cdns_xspi_of_match[] = {
	{
		.compatible = "cdns,xspi-nor",
		.data = &cdns_driver_data,
	},
	{
		.compatible = "marvell,cn10-xspi-nor",
		.data = &marvell_driver_data,
	},
	{ /* end of table */}
};
MODULE_DEVICE_TABLE(of, cdns_xspi_of_match);

static struct platform_driver cdns_xspi_platform_driver = {
	.probe          = cdns_xspi_probe,
	.driver = {
		.name = CDNS_XSPI_NAME,
		.of_match_table = cdns_xspi_of_match,
	},
};

module_platform_driver(cdns_xspi_platform_driver);

MODULE_DESCRIPTION("Cadence XSPI Controller Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" CDNS_XSPI_NAME);
MODULE_AUTHOR("Konrad Kociolek <konrad@cadence.com>");
MODULE_AUTHOR("Jayshri Pawar <jpawar@cadence.com>");
MODULE_AUTHOR("Parshuram Thombare <pthombar@cadence.com>");
