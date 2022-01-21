// SPDX-License-Identifier: GPL-2.0+
// Cadence XSPI flash controller driver
// Copyright (C) 2020-21 Cadence

#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>
#include <linux/bitfield.h>
#include <linux/limits.h>
#include <linux/log2.h>

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

#define CDNS_XSPI_CMD_FLD_P1_INSTR_CMD_3(op) ( \
	FIELD_PREP(CDNS_XSPI_CMD_P1_R3_ADDR5, ((op)->addr.val >> 40) & 0xFF) | \
	FIELD_PREP(CDNS_XSPI_CMD_P1_R3_CMD, (op)->cmd.opcode) | \
	FIELD_PREP(CDNS_XSPI_CMD_P1_R3_NUM_ADDR_BYTES, (op)->addr.nbytes))

#define CDNS_XSPI_CMD_FLD_P1_INSTR_CMD_4(op, chipsel) ( \
	FIELD_PREP(CDNS_XSPI_CMD_P1_R4_ADDR_IOS, ilog2((op)->addr.buswidth)) | \
	FIELD_PREP(CDNS_XSPI_CMD_P1_R4_CMD_IOS, ilog2((op)->cmd.buswidth)) | \
	FIELD_PREP(CDNS_XSPI_CMD_P1_R4_BANK, chipsel))

#define CDNS_XSPI_CMD_FLD_DSEQ_CMD_1(op) \
	FIELD_PREP(CDNS_XSPI_CMD_INSTR_TYPE, CDNS_XSPI_STIG_INSTR_TYPE_DATA_SEQ)

#define CDNS_XSPI_CMD_FLD_DSEQ_CMD_2(op) \
	FIELD_PREP(CDNS_XSPI_CMD_DSEQ_R2_DCNT_L, (op)->data.nbytes & 0xFFFF)

#define CDNS_XSPI_CMD_FLD_DSEQ_CMD_3(op) ( \
	FIELD_PREP(CDNS_XSPI_CMD_DSEQ_R3_DCNT_H, \
		((op)->data.nbytes >> 16) & 0xffff) | \
	FIELD_PREP(CDNS_XSPI_CMD_DSEQ_R3_NUM_OF_DUMMY, (op)->dummy.nbytes * 8))

#define CDNS_XSPI_CMD_FLD_DSEQ_CMD_4(op, chipsel) ( \
	FIELD_PREP(CDNS_XSPI_CMD_DSEQ_R4_BANK, chipsel) | \
	FIELD_PREP(CDNS_XSPI_CMD_DSEQ_R4_DATA_IOS, \
		ilog2((op)->data.buswidth)) | \
	FIELD_PREP(CDNS_XSPI_CMD_DSEQ_R4_DIR, \
		((op)->data.dir == SPI_MEM_DATA_IN) ? \
		CDNS_XSPI_STIG_CMD_DIR_READ : CDNS_XSPI_STIG_CMD_DIR_WRITE))

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

struct cdns_xspi_dev {
	struct platform_device *pdev;
	struct device *dev;

	void __iomem *iobase;
	void __iomem *auxbase;
	void __iomem *sdmabase;

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
};

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
	cdns_xspi_set_interrupts(cdns_xspi, false);

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

static int cdns_xspi_send_stig_command(struct cdns_xspi_dev *cdns_xspi,
				       const struct spi_mem_op *op,
				       bool data_phase)
{
	u32 cmd_regs[6];
	u32 cmd_status;
	int ret;

	ret = cdns_xspi_wait_for_controller_idle(cdns_xspi);
	if (ret < 0)
		return -EIO;

	writel(FIELD_PREP(CDNS_XSPI_CTRL_WORK_MODE, CDNS_XSPI_WORK_MODE_STIG),
	       cdns_xspi->iobase + CDNS_XSPI_CTRL_CONFIG_REG);

	cdns_xspi_set_interrupts(cdns_xspi, true);
	cdns_xspi->sdma_error = false;

	memset(cmd_regs, 0, sizeof(cmd_regs));
	cmd_regs[1] = CDNS_XSPI_CMD_FLD_P1_INSTR_CMD_1(op, data_phase);
	cmd_regs[2] = CDNS_XSPI_CMD_FLD_P1_INSTR_CMD_2(op);
	cmd_regs[3] = CDNS_XSPI_CMD_FLD_P1_INSTR_CMD_3(op);
	cmd_regs[4] = CDNS_XSPI_CMD_FLD_P1_INSTR_CMD_4(op,
						       cdns_xspi->cur_cs);

	cdns_xspi_trigger_command(cdns_xspi, cmd_regs);

	if (data_phase) {
		cmd_regs[0] = CDNS_XSPI_STIG_DONE_FLAG;
		cmd_regs[1] = CDNS_XSPI_CMD_FLD_DSEQ_CMD_1(op);
		cmd_regs[2] = CDNS_XSPI_CMD_FLD_DSEQ_CMD_2(op);
		cmd_regs[3] = CDNS_XSPI_CMD_FLD_DSEQ_CMD_3(op);
		cmd_regs[4] = CDNS_XSPI_CMD_FLD_DSEQ_CMD_4(op,
							   cdns_xspi->cur_cs);

		cdns_xspi->in_buffer = op->data.buf.in;
		cdns_xspi->out_buffer = op->data.buf.out;

		cdns_xspi_trigger_command(cdns_xspi, cmd_regs);

		wait_for_completion(&cdns_xspi->sdma_complete);
		if (cdns_xspi->sdma_error) {
			cdns_xspi_set_interrupts(cdns_xspi, false);
			return -EIO;
		}
		cdns_xspi_sdma_handle(cdns_xspi);
	}

	wait_for_completion(&cdns_xspi->cmd_complete);
	cdns_xspi_set_interrupts(cdns_xspi, false);

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

	if (cdns_xspi->cur_cs != mem->spi->chip_select)
		cdns_xspi->cur_cs = mem->spi->chip_select;

	return cdns_xspi_send_stig_command(cdns_xspi, op,
					   (dir != SPI_MEM_NO_DATA));
}

static int cdns_xspi_mem_op_execute(struct spi_mem *mem,
				    const struct spi_mem_op *op)
{
	struct cdns_xspi_dev *cdns_xspi =
		spi_master_get_devdata(mem->spi->master);
	int ret = 0;

	ret = cdns_xspi_mem_op(cdns_xspi, mem, op);

	return ret;
}

static int cdns_xspi_adjust_mem_op_size(struct spi_mem *mem, struct spi_mem_op *op)
{
	struct cdns_xspi_dev *cdns_xspi =
		spi_master_get_devdata(mem->spi->master);

	op->data.nbytes = clamp_val(op->data.nbytes, 0, cdns_xspi->sdmasize);

	return 0;
}

static const struct spi_controller_mem_ops cadence_xspi_mem_ops = {
	.exec_op = cdns_xspi_mem_op_execute,
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
	struct device_node *node_prop = pdev->dev.of_node;
	struct device_node *node_child;
	unsigned int cs;

	for_each_child_of_node(node_prop, node_child) {
		if (!of_device_is_available(node_child))
			continue;

		if (of_property_read_u32(node_child, "reg", &cs)) {
			dev_err(&pdev->dev, "Couldn't get memory chip select\n");
			of_node_put(node_child);
			return -ENXIO;
		} else if (cs >= CDNS_XSPI_MAX_BANKS) {
			dev_err(&pdev->dev, "reg (cs) parameter value too large\n");
			of_node_put(node_child);
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

static int cdns_xspi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct spi_master *master = NULL;
	struct cdns_xspi_dev *cdns_xspi = NULL;
	struct resource *res;
	int ret;

	master = devm_spi_alloc_master(dev, sizeof(*cdns_xspi));
	if (!master)
		return -ENOMEM;

	master->mode_bits = SPI_3WIRE | SPI_TX_DUAL  | SPI_TX_QUAD  |
		SPI_RX_DUAL | SPI_RX_QUAD | SPI_TX_OCTAL | SPI_RX_OCTAL |
		SPI_MODE_0  | SPI_MODE_3;

	master->mem_ops = &cadence_xspi_mem_ops;
	master->dev.of_node = pdev->dev.of_node;
	master->bus_num = -1;

	platform_set_drvdata(pdev, master);

	cdns_xspi = spi_master_get_devdata(master);
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
		dev_err(dev, "Failed to remap controller base address\n");
		return PTR_ERR(cdns_xspi->iobase);
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "sdma");
	cdns_xspi->sdmabase = devm_ioremap_resource(dev, res);
	if (IS_ERR(cdns_xspi->sdmabase)) {
		dev_err(dev, "Failed to remap SDMA address\n");
		return PTR_ERR(cdns_xspi->sdmabase);
	}
	cdns_xspi->sdmasize = resource_size(res);

	cdns_xspi->auxbase = devm_platform_ioremap_resource_byname(pdev, "aux");
	if (IS_ERR(cdns_xspi->auxbase)) {
		dev_err(dev, "Failed to remap AUX address\n");
		return PTR_ERR(cdns_xspi->auxbase);
	}

	cdns_xspi->irq = platform_get_irq(pdev, 0);
	if (cdns_xspi->irq < 0) {
		dev_err(dev, "Failed to get IRQ\n");
		return -ENXIO;
	}

	ret = devm_request_irq(dev, cdns_xspi->irq, cdns_xspi_irq_handler,
			       IRQF_SHARED, pdev->name, cdns_xspi);
	if (ret) {
		dev_err(dev, "Failed to request IRQ: %d\n", cdns_xspi->irq);
		return ret;
	}

	cdns_xspi_print_phy_config(cdns_xspi);

	ret = cdns_xspi_controller_init(cdns_xspi);
	if (ret) {
		dev_err(dev, "Failed to initialize controller\n");
		return ret;
	}

	master->num_chipselect = 1 << cdns_xspi->hw_num_banks;

	ret = devm_spi_register_master(dev, master);
	if (ret) {
		dev_err(dev, "Failed to register SPI master\n");
		return ret;
	}

	dev_info(dev, "Successfully registered SPI master\n");

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id cdns_xspi_of_match[] = {
	{
		.compatible = "cdns,xspi-nor",
	},
	{ /* end of table */}
};
MODULE_DEVICE_TABLE(of, cdns_xspi_of_match);
#else
#define cdns_xspi_of_match NULL
#endif /* CONFIG_OF */

static struct platform_driver cdns_xspi_platform_driver = {
	.probe          = cdns_xspi_probe,
	.remove         = NULL,
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
