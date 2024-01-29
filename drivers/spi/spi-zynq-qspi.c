// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2019 Xilinx, Inc.
 *
 * Author: Naga Sureshkumar Relli <nagasure@xilinx.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/workqueue.h>
#include <linux/spi/spi-mem.h>

/* Register offset definitions */
#define ZYNQ_QSPI_CONFIG_OFFSET		0x00 /* Configuration  Register, RW */
#define ZYNQ_QSPI_STATUS_OFFSET		0x04 /* Interrupt Status Register, RO */
#define ZYNQ_QSPI_IEN_OFFSET		0x08 /* Interrupt Enable Register, WO */
#define ZYNQ_QSPI_IDIS_OFFSET		0x0C /* Interrupt Disable Reg, WO */
#define ZYNQ_QSPI_IMASK_OFFSET		0x10 /* Interrupt Enabled Mask Reg,RO */
#define ZYNQ_QSPI_ENABLE_OFFSET		0x14 /* Enable/Disable Register, RW */
#define ZYNQ_QSPI_DELAY_OFFSET		0x18 /* Delay Register, RW */
#define ZYNQ_QSPI_TXD_00_00_OFFSET	0x1C /* Transmit 4-byte inst, WO */
#define ZYNQ_QSPI_TXD_00_01_OFFSET	0x80 /* Transmit 1-byte inst, WO */
#define ZYNQ_QSPI_TXD_00_10_OFFSET	0x84 /* Transmit 2-byte inst, WO */
#define ZYNQ_QSPI_TXD_00_11_OFFSET	0x88 /* Transmit 3-byte inst, WO */
#define ZYNQ_QSPI_RXD_OFFSET		0x20 /* Data Receive Register, RO */
#define ZYNQ_QSPI_SIC_OFFSET		0x24 /* Slave Idle Count Register, RW */
#define ZYNQ_QSPI_TX_THRESH_OFFSET	0x28 /* TX FIFO Watermark Reg, RW */
#define ZYNQ_QSPI_RX_THRESH_OFFSET	0x2C /* RX FIFO Watermark Reg, RW */
#define ZYNQ_QSPI_GPIO_OFFSET		0x30 /* GPIO Register, RW */
#define ZYNQ_QSPI_LINEAR_CFG_OFFSET	0xA0 /* Linear Adapter Config Ref, RW */
#define ZYNQ_QSPI_MOD_ID_OFFSET		0xFC /* Module ID Register, RO */

/*
 * QSPI Configuration Register bit Masks
 *
 * This register contains various control bits that effect the operation
 * of the QSPI controller
 */
#define ZYNQ_QSPI_CONFIG_IFMODE_MASK	BIT(31) /* Flash Memory Interface */
#define ZYNQ_QSPI_CONFIG_MANSRT_MASK	BIT(16) /* Manual TX Start */
#define ZYNQ_QSPI_CONFIG_MANSRTEN_MASK	BIT(15) /* Enable Manual TX Mode */
#define ZYNQ_QSPI_CONFIG_SSFORCE_MASK	BIT(14) /* Manual Chip Select */
#define ZYNQ_QSPI_CONFIG_BDRATE_MASK	GENMASK(5, 3) /* Baud Rate Mask */
#define ZYNQ_QSPI_CONFIG_CPHA_MASK	BIT(2) /* Clock Phase Control */
#define ZYNQ_QSPI_CONFIG_CPOL_MASK	BIT(1) /* Clock Polarity Control */
#define ZYNQ_QSPI_CONFIG_FWIDTH_MASK	GENMASK(7, 6) /* FIFO width */
#define ZYNQ_QSPI_CONFIG_MSTREN_MASK	BIT(0) /* Master Mode */

/*
 * QSPI Configuration Register - Baud rate and target select
 *
 * These are the values used in the calculation of baud rate divisor and
 * setting the target select.
 */
#define ZYNQ_QSPI_CONFIG_BAUD_DIV_MAX	GENMASK(2, 0) /* Baud rate maximum */
#define ZYNQ_QSPI_CONFIG_BAUD_DIV_SHIFT	3 /* Baud rate divisor shift */
#define ZYNQ_QSPI_CONFIG_PCS		BIT(10) /* Peripheral Chip Select */

/*
 * QSPI Interrupt Registers bit Masks
 *
 * All the four interrupt registers (Status/Mask/Enable/Disable) have the same
 * bit definitions.
 */
#define ZYNQ_QSPI_IXR_RX_OVERFLOW_MASK	BIT(0) /* QSPI RX FIFO Overflow */
#define ZYNQ_QSPI_IXR_TXNFULL_MASK	BIT(2) /* QSPI TX FIFO Overflow */
#define ZYNQ_QSPI_IXR_TXFULL_MASK	BIT(3) /* QSPI TX FIFO is full */
#define ZYNQ_QSPI_IXR_RXNEMTY_MASK	BIT(4) /* QSPI RX FIFO Not Empty */
#define ZYNQ_QSPI_IXR_RXF_FULL_MASK	BIT(5) /* QSPI RX FIFO is full */
#define ZYNQ_QSPI_IXR_TXF_UNDRFLOW_MASK	BIT(6) /* QSPI TX FIFO Underflow */
#define ZYNQ_QSPI_IXR_ALL_MASK		(ZYNQ_QSPI_IXR_RX_OVERFLOW_MASK | \
					ZYNQ_QSPI_IXR_TXNFULL_MASK | \
					ZYNQ_QSPI_IXR_TXFULL_MASK | \
					ZYNQ_QSPI_IXR_RXNEMTY_MASK | \
					ZYNQ_QSPI_IXR_RXF_FULL_MASK | \
					ZYNQ_QSPI_IXR_TXF_UNDRFLOW_MASK)
#define ZYNQ_QSPI_IXR_RXTX_MASK		(ZYNQ_QSPI_IXR_TXNFULL_MASK | \
					ZYNQ_QSPI_IXR_RXNEMTY_MASK)

/*
 * QSPI Enable Register bit Masks
 *
 * This register is used to enable or disable the QSPI controller
 */
#define ZYNQ_QSPI_ENABLE_ENABLE_MASK	BIT(0) /* QSPI Enable Bit Mask */

/*
 * QSPI Linear Configuration Register
 *
 * It is named Linear Configuration but it controls other modes when not in
 * linear mode also.
 */
#define ZYNQ_QSPI_LCFG_TWO_MEM		BIT(30) /* LQSPI Two memories */
#define ZYNQ_QSPI_LCFG_SEP_BUS		BIT(29) /* LQSPI Separate bus */
#define ZYNQ_QSPI_LCFG_U_PAGE		BIT(28) /* LQSPI Upper Page */

#define ZYNQ_QSPI_LCFG_DUMMY_SHIFT	8

#define ZYNQ_QSPI_FAST_READ_QOUT_CODE	0x6B /* read instruction code */
#define ZYNQ_QSPI_FIFO_DEPTH		63 /* FIFO depth in words */
#define ZYNQ_QSPI_RX_THRESHOLD		32 /* Rx FIFO threshold level */
#define ZYNQ_QSPI_TX_THRESHOLD		1 /* Tx FIFO threshold level */

/*
 * The modebits configurable by the driver to make the SPI support different
 * data formats
 */
#define ZYNQ_QSPI_MODEBITS			(SPI_CPOL | SPI_CPHA)

/* Maximum number of chip selects */
#define ZYNQ_QSPI_MAX_NUM_CS		2

/**
 * struct zynq_qspi - Defines qspi driver instance
 * @dev:		Pointer to the this device's information
 * @regs:		Virtual address of the QSPI controller registers
 * @refclk:		Pointer to the peripheral clock
 * @pclk:		Pointer to the APB clock
 * @irq:		IRQ number
 * @txbuf:		Pointer to the TX buffer
 * @rxbuf:		Pointer to the RX buffer
 * @tx_bytes:		Number of bytes left to transfer
 * @rx_bytes:		Number of bytes left to receive
 * @data_completion:	completion structure
 */
struct zynq_qspi {
	struct device *dev;
	void __iomem *regs;
	struct clk *refclk;
	struct clk *pclk;
	int irq;
	u8 *txbuf;
	u8 *rxbuf;
	int tx_bytes;
	int rx_bytes;
	struct completion data_completion;
};

/*
 * Inline functions for the QSPI controller read/write
 */
static inline u32 zynq_qspi_read(struct zynq_qspi *xqspi, u32 offset)
{
	return readl_relaxed(xqspi->regs + offset);
}

static inline void zynq_qspi_write(struct zynq_qspi *xqspi, u32 offset,
				   u32 val)
{
	writel_relaxed(val, xqspi->regs + offset);
}

/**
 * zynq_qspi_init_hw - Initialize the hardware
 * @xqspi:	Pointer to the zynq_qspi structure
 * @num_cs:	Number of connected CS (to enable dual memories if needed)
 *
 * The default settings of the QSPI controller's configurable parameters on
 * reset are
 *	- Host mode
 *	- Baud rate divisor is set to 2
 *	- Tx threshold set to 1l Rx threshold set to 32
 *	- Flash memory interface mode enabled
 *	- Size of the word to be transferred as 8 bit
 * This function performs the following actions
 *	- Disable and clear all the interrupts
 *	- Enable manual target select
 *	- Enable manual start
 *	- Deselect all the chip select lines
 *	- Set the size of the word to be transferred as 32 bit
 *	- Set the little endian mode of TX FIFO and
 *	- Enable the QSPI controller
 */
static void zynq_qspi_init_hw(struct zynq_qspi *xqspi, unsigned int num_cs)
{
	u32 config_reg;

	zynq_qspi_write(xqspi, ZYNQ_QSPI_ENABLE_OFFSET, 0);
	zynq_qspi_write(xqspi, ZYNQ_QSPI_IDIS_OFFSET, ZYNQ_QSPI_IXR_ALL_MASK);

	/* Disable linear mode as the boot loader may have used it */
	config_reg = 0;
	/* At the same time, enable dual mode if more than 1 CS is available */
	if (num_cs > 1)
		config_reg |= ZYNQ_QSPI_LCFG_TWO_MEM;

	zynq_qspi_write(xqspi, ZYNQ_QSPI_LINEAR_CFG_OFFSET, config_reg);

	/* Clear the RX FIFO */
	while (zynq_qspi_read(xqspi, ZYNQ_QSPI_STATUS_OFFSET) &
			      ZYNQ_QSPI_IXR_RXNEMTY_MASK)
		zynq_qspi_read(xqspi, ZYNQ_QSPI_RXD_OFFSET);

	zynq_qspi_write(xqspi, ZYNQ_QSPI_STATUS_OFFSET, ZYNQ_QSPI_IXR_ALL_MASK);
	config_reg = zynq_qspi_read(xqspi, ZYNQ_QSPI_CONFIG_OFFSET);
	config_reg &= ~(ZYNQ_QSPI_CONFIG_MSTREN_MASK |
			ZYNQ_QSPI_CONFIG_CPOL_MASK |
			ZYNQ_QSPI_CONFIG_CPHA_MASK |
			ZYNQ_QSPI_CONFIG_BDRATE_MASK |
			ZYNQ_QSPI_CONFIG_SSFORCE_MASK |
			ZYNQ_QSPI_CONFIG_MANSRTEN_MASK |
			ZYNQ_QSPI_CONFIG_MANSRT_MASK);
	config_reg |= (ZYNQ_QSPI_CONFIG_MSTREN_MASK |
		       ZYNQ_QSPI_CONFIG_SSFORCE_MASK |
		       ZYNQ_QSPI_CONFIG_FWIDTH_MASK |
		       ZYNQ_QSPI_CONFIG_IFMODE_MASK);
	zynq_qspi_write(xqspi, ZYNQ_QSPI_CONFIG_OFFSET, config_reg);

	zynq_qspi_write(xqspi, ZYNQ_QSPI_RX_THRESH_OFFSET,
			ZYNQ_QSPI_RX_THRESHOLD);
	zynq_qspi_write(xqspi, ZYNQ_QSPI_TX_THRESH_OFFSET,
			ZYNQ_QSPI_TX_THRESHOLD);

	zynq_qspi_write(xqspi, ZYNQ_QSPI_ENABLE_OFFSET,
			ZYNQ_QSPI_ENABLE_ENABLE_MASK);
}

static bool zynq_qspi_supports_op(struct spi_mem *mem,
				  const struct spi_mem_op *op)
{
	if (!spi_mem_default_supports_op(mem, op))
		return false;

	/*
	 * The number of address bytes should be equal to or less than 3 bytes.
	 */
	if (op->addr.nbytes > 3)
		return false;

	return true;
}

/**
 * zynq_qspi_rxfifo_op - Read 1..4 bytes from RxFIFO to RX buffer
 * @xqspi:	Pointer to the zynq_qspi structure
 * @size:	Number of bytes to be read (1..4)
 */
static void zynq_qspi_rxfifo_op(struct zynq_qspi *xqspi, unsigned int size)
{
	u32 data;

	data = zynq_qspi_read(xqspi, ZYNQ_QSPI_RXD_OFFSET);

	if (xqspi->rxbuf) {
		memcpy(xqspi->rxbuf, ((u8 *)&data) + 4 - size, size);
		xqspi->rxbuf += size;
	}

	xqspi->rx_bytes -= size;
	if (xqspi->rx_bytes < 0)
		xqspi->rx_bytes = 0;
}

/**
 * zynq_qspi_txfifo_op - Write 1..4 bytes from TX buffer to TxFIFO
 * @xqspi:	Pointer to the zynq_qspi structure
 * @size:	Number of bytes to be written (1..4)
 */
static void zynq_qspi_txfifo_op(struct zynq_qspi *xqspi, unsigned int size)
{
	static const unsigned int offset[4] = {
		ZYNQ_QSPI_TXD_00_01_OFFSET, ZYNQ_QSPI_TXD_00_10_OFFSET,
		ZYNQ_QSPI_TXD_00_11_OFFSET, ZYNQ_QSPI_TXD_00_00_OFFSET };
	u32 data;

	if (xqspi->txbuf) {
		data = 0xffffffff;
		memcpy(&data, xqspi->txbuf, size);
		xqspi->txbuf += size;
	} else {
		data = 0;
	}

	xqspi->tx_bytes -= size;
	zynq_qspi_write(xqspi, offset[size - 1], data);
}

/**
 * zynq_qspi_chipselect - Select or deselect the chip select line
 * @spi:	Pointer to the spi_device structure
 * @assert:	1 for select or 0 for deselect the chip select line
 */
static void zynq_qspi_chipselect(struct spi_device *spi, bool assert)
{
	struct spi_controller *ctlr = spi->controller;
	struct zynq_qspi *xqspi = spi_controller_get_devdata(ctlr);
	u32 config_reg;

	/* Select the lower (CS0) or upper (CS1) memory */
	if (ctlr->num_chipselect > 1) {
		config_reg = zynq_qspi_read(xqspi, ZYNQ_QSPI_LINEAR_CFG_OFFSET);
		if (!spi_get_chipselect(spi, 0))
			config_reg &= ~ZYNQ_QSPI_LCFG_U_PAGE;
		else
			config_reg |= ZYNQ_QSPI_LCFG_U_PAGE;

		zynq_qspi_write(xqspi, ZYNQ_QSPI_LINEAR_CFG_OFFSET, config_reg);
	}

	/* Ground the line to assert the CS */
	config_reg = zynq_qspi_read(xqspi, ZYNQ_QSPI_CONFIG_OFFSET);
	if (assert)
		config_reg &= ~ZYNQ_QSPI_CONFIG_PCS;
	else
		config_reg |= ZYNQ_QSPI_CONFIG_PCS;

	zynq_qspi_write(xqspi, ZYNQ_QSPI_CONFIG_OFFSET, config_reg);
}

/**
 * zynq_qspi_config_op - Configure QSPI controller for specified transfer
 * @xqspi:	Pointer to the zynq_qspi structure
 * @spi:	Pointer to the spi_device structure
 *
 * Sets the operational mode of QSPI controller for the next QSPI transfer and
 * sets the requested clock frequency.
 *
 * Return:	0 on success and -EINVAL on invalid input parameter
 *
 * Note: If the requested frequency is not an exact match with what can be
 * obtained using the prescalar value, the driver sets the clock frequency which
 * is lower than the requested frequency (maximum lower) for the transfer. If
 * the requested frequency is higher or lower than that is supported by the QSPI
 * controller the driver will set the highest or lowest frequency supported by
 * controller.
 */
static int zynq_qspi_config_op(struct zynq_qspi *xqspi, struct spi_device *spi)
{
	u32 config_reg, baud_rate_val = 0;

	/*
	 * Set the clock frequency
	 * The baud rate divisor is not a direct mapping to the value written
	 * into the configuration register (config_reg[5:3])
	 * i.e. 000 - divide by 2
	 *      001 - divide by 4
	 *      ----------------
	 *      111 - divide by 256
	 */
	while ((baud_rate_val < ZYNQ_QSPI_CONFIG_BAUD_DIV_MAX)  &&
	       (clk_get_rate(xqspi->refclk) / (2 << baud_rate_val)) >
		spi->max_speed_hz)
		baud_rate_val++;

	config_reg = zynq_qspi_read(xqspi, ZYNQ_QSPI_CONFIG_OFFSET);

	/* Set the QSPI clock phase and clock polarity */
	config_reg &= (~ZYNQ_QSPI_CONFIG_CPHA_MASK) &
		      (~ZYNQ_QSPI_CONFIG_CPOL_MASK);
	if (spi->mode & SPI_CPHA)
		config_reg |= ZYNQ_QSPI_CONFIG_CPHA_MASK;
	if (spi->mode & SPI_CPOL)
		config_reg |= ZYNQ_QSPI_CONFIG_CPOL_MASK;

	config_reg &= ~ZYNQ_QSPI_CONFIG_BDRATE_MASK;
	config_reg |= (baud_rate_val << ZYNQ_QSPI_CONFIG_BAUD_DIV_SHIFT);
	zynq_qspi_write(xqspi, ZYNQ_QSPI_CONFIG_OFFSET, config_reg);

	return 0;
}

/**
 * zynq_qspi_setup_op - Configure the QSPI controller
 * @spi:	Pointer to the spi_device structure
 *
 * Sets the operational mode of QSPI controller for the next QSPI transfer, baud
 * rate and divisor value to setup the requested qspi clock.
 *
 * Return:	0 on success and error value on failure
 */
static int zynq_qspi_setup_op(struct spi_device *spi)
{
	struct spi_controller *ctlr = spi->controller;
	struct zynq_qspi *qspi = spi_controller_get_devdata(ctlr);

	if (ctlr->busy)
		return -EBUSY;

	clk_enable(qspi->refclk);
	clk_enable(qspi->pclk);
	zynq_qspi_write(qspi, ZYNQ_QSPI_ENABLE_OFFSET,
			ZYNQ_QSPI_ENABLE_ENABLE_MASK);

	return 0;
}

/**
 * zynq_qspi_write_op - Fills the TX FIFO with as many bytes as possible
 * @xqspi:	Pointer to the zynq_qspi structure
 * @txcount:	Maximum number of words to write
 * @txempty:	Indicates that TxFIFO is empty
 */
static void zynq_qspi_write_op(struct zynq_qspi *xqspi, int txcount,
			       bool txempty)
{
	int count, len, k;

	len = xqspi->tx_bytes;
	if (len && len < 4) {
		/*
		 * We must empty the TxFIFO between accesses to TXD0,
		 * TXD1, TXD2, TXD3.
		 */
		if (txempty)
			zynq_qspi_txfifo_op(xqspi, len);

		return;
	}

	count = len / 4;
	if (count > txcount)
		count = txcount;

	if (xqspi->txbuf) {
		iowrite32_rep(xqspi->regs + ZYNQ_QSPI_TXD_00_00_OFFSET,
			      xqspi->txbuf, count);
		xqspi->txbuf += count * 4;
	} else {
		for (k = 0; k < count; k++)
			writel_relaxed(0, xqspi->regs +
					  ZYNQ_QSPI_TXD_00_00_OFFSET);
	}

	xqspi->tx_bytes -= count * 4;
}

/**
 * zynq_qspi_read_op - Drains the RX FIFO by as many bytes as possible
 * @xqspi:	Pointer to the zynq_qspi structure
 * @rxcount:	Maximum number of words to read
 */
static void zynq_qspi_read_op(struct zynq_qspi *xqspi, int rxcount)
{
	int count, len, k;

	len = xqspi->rx_bytes - xqspi->tx_bytes;
	count = len / 4;
	if (count > rxcount)
		count = rxcount;
	if (xqspi->rxbuf) {
		ioread32_rep(xqspi->regs + ZYNQ_QSPI_RXD_OFFSET,
			     xqspi->rxbuf, count);
		xqspi->rxbuf += count * 4;
	} else {
		for (k = 0; k < count; k++)
			readl_relaxed(xqspi->regs + ZYNQ_QSPI_RXD_OFFSET);
	}
	xqspi->rx_bytes -= count * 4;
	len -= count * 4;

	if (len && len < 4 && count < rxcount)
		zynq_qspi_rxfifo_op(xqspi, len);
}

/**
 * zynq_qspi_irq - Interrupt service routine of the QSPI controller
 * @irq:	IRQ number
 * @dev_id:	Pointer to the xqspi structure
 *
 * This function handles TX empty only.
 * On TX empty interrupt this function reads the received data from RX FIFO and
 * fills the TX FIFO if there is any data remaining to be transferred.
 *
 * Return:	IRQ_HANDLED when interrupt is handled; IRQ_NONE otherwise.
 */
static irqreturn_t zynq_qspi_irq(int irq, void *dev_id)
{
	u32 intr_status;
	bool txempty;
	struct zynq_qspi *xqspi = (struct zynq_qspi *)dev_id;

	intr_status = zynq_qspi_read(xqspi, ZYNQ_QSPI_STATUS_OFFSET);
	zynq_qspi_write(xqspi, ZYNQ_QSPI_STATUS_OFFSET, intr_status);

	if ((intr_status & ZYNQ_QSPI_IXR_TXNFULL_MASK) ||
	    (intr_status & ZYNQ_QSPI_IXR_RXNEMTY_MASK)) {
		/*
		 * This bit is set when Tx FIFO has < THRESHOLD entries.
		 * We have the THRESHOLD value set to 1,
		 * so this bit indicates Tx FIFO is empty.
		 */
		txempty = !!(intr_status & ZYNQ_QSPI_IXR_TXNFULL_MASK);
		/* Read out the data from the RX FIFO */
		zynq_qspi_read_op(xqspi, ZYNQ_QSPI_RX_THRESHOLD);
		if (xqspi->tx_bytes) {
			/* There is more data to send */
			zynq_qspi_write_op(xqspi, ZYNQ_QSPI_RX_THRESHOLD,
					   txempty);
		} else {
			/*
			 * If transfer and receive is completed then only send
			 * complete signal.
			 */
			if (!xqspi->rx_bytes) {
				zynq_qspi_write(xqspi,
						ZYNQ_QSPI_IDIS_OFFSET,
						ZYNQ_QSPI_IXR_RXTX_MASK);
				complete(&xqspi->data_completion);
			}
		}
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

/**
 * zynq_qspi_exec_mem_op() - Initiates the QSPI transfer
 * @mem: the SPI memory
 * @op: the memory operation to execute
 *
 * Executes a memory operation.
 *
 * This function first selects the chip and starts the memory operation.
 *
 * Return: 0 in case of success, a negative error code otherwise.
 */
static int zynq_qspi_exec_mem_op(struct spi_mem *mem,
				 const struct spi_mem_op *op)
{
	struct zynq_qspi *xqspi = spi_controller_get_devdata(mem->spi->controller);
	int err = 0, i;
	u8 *tmpbuf;

	dev_dbg(xqspi->dev, "cmd:%#x mode:%d.%d.%d.%d\n",
		op->cmd.opcode, op->cmd.buswidth, op->addr.buswidth,
		op->dummy.buswidth, op->data.buswidth);

	zynq_qspi_chipselect(mem->spi, true);
	zynq_qspi_config_op(xqspi, mem->spi);

	if (op->cmd.opcode) {
		reinit_completion(&xqspi->data_completion);
		xqspi->txbuf = (u8 *)&op->cmd.opcode;
		xqspi->rxbuf = NULL;
		xqspi->tx_bytes = op->cmd.nbytes;
		xqspi->rx_bytes = op->cmd.nbytes;
		zynq_qspi_write_op(xqspi, ZYNQ_QSPI_FIFO_DEPTH, true);
		zynq_qspi_write(xqspi, ZYNQ_QSPI_IEN_OFFSET,
				ZYNQ_QSPI_IXR_RXTX_MASK);
		if (!wait_for_completion_timeout(&xqspi->data_completion,
							       msecs_to_jiffies(1000)))
			err = -ETIMEDOUT;
	}

	if (op->addr.nbytes) {
		for (i = 0; i < op->addr.nbytes; i++) {
			xqspi->txbuf[i] = op->addr.val >>
					(8 * (op->addr.nbytes - i - 1));
		}

		reinit_completion(&xqspi->data_completion);
		xqspi->rxbuf = NULL;
		xqspi->tx_bytes = op->addr.nbytes;
		xqspi->rx_bytes = op->addr.nbytes;
		zynq_qspi_write_op(xqspi, ZYNQ_QSPI_FIFO_DEPTH, true);
		zynq_qspi_write(xqspi, ZYNQ_QSPI_IEN_OFFSET,
				ZYNQ_QSPI_IXR_RXTX_MASK);
		if (!wait_for_completion_timeout(&xqspi->data_completion,
							       msecs_to_jiffies(1000)))
			err = -ETIMEDOUT;
	}

	if (op->dummy.nbytes) {
		tmpbuf = kzalloc(op->dummy.nbytes, GFP_KERNEL);
		if (!tmpbuf)
			return -ENOMEM;

		memset(tmpbuf, 0xff, op->dummy.nbytes);
		reinit_completion(&xqspi->data_completion);
		xqspi->txbuf = tmpbuf;
		xqspi->rxbuf = NULL;
		xqspi->tx_bytes = op->dummy.nbytes;
		xqspi->rx_bytes = op->dummy.nbytes;
		zynq_qspi_write_op(xqspi, ZYNQ_QSPI_FIFO_DEPTH, true);
		zynq_qspi_write(xqspi, ZYNQ_QSPI_IEN_OFFSET,
				ZYNQ_QSPI_IXR_RXTX_MASK);
		if (!wait_for_completion_timeout(&xqspi->data_completion,
							       msecs_to_jiffies(1000)))
			err = -ETIMEDOUT;

		kfree(tmpbuf);
	}

	if (op->data.nbytes) {
		reinit_completion(&xqspi->data_completion);
		if (op->data.dir == SPI_MEM_DATA_OUT) {
			xqspi->txbuf = (u8 *)op->data.buf.out;
			xqspi->tx_bytes = op->data.nbytes;
			xqspi->rxbuf = NULL;
			xqspi->rx_bytes = op->data.nbytes;
		} else {
			xqspi->txbuf = NULL;
			xqspi->rxbuf = (u8 *)op->data.buf.in;
			xqspi->rx_bytes = op->data.nbytes;
			xqspi->tx_bytes = op->data.nbytes;
		}

		zynq_qspi_write_op(xqspi, ZYNQ_QSPI_FIFO_DEPTH, true);
		zynq_qspi_write(xqspi, ZYNQ_QSPI_IEN_OFFSET,
				ZYNQ_QSPI_IXR_RXTX_MASK);
		if (!wait_for_completion_timeout(&xqspi->data_completion,
							       msecs_to_jiffies(1000)))
			err = -ETIMEDOUT;
	}
	zynq_qspi_chipselect(mem->spi, false);

	return err;
}

static const struct spi_controller_mem_ops zynq_qspi_mem_ops = {
	.supports_op = zynq_qspi_supports_op,
	.exec_op = zynq_qspi_exec_mem_op,
};

/**
 * zynq_qspi_probe - Probe method for the QSPI driver
 * @pdev:	Pointer to the platform_device structure
 *
 * This function initializes the driver data structures and the hardware.
 *
 * Return:	0 on success and error value on failure
 */
static int zynq_qspi_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct spi_controller *ctlr;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct zynq_qspi *xqspi;
	u32 num_cs;

	ctlr = spi_alloc_host(&pdev->dev, sizeof(*xqspi));
	if (!ctlr)
		return -ENOMEM;

	xqspi = spi_controller_get_devdata(ctlr);
	xqspi->dev = dev;
	platform_set_drvdata(pdev, xqspi);
	xqspi->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(xqspi->regs)) {
		ret = PTR_ERR(xqspi->regs);
		goto remove_ctlr;
	}

	xqspi->pclk = devm_clk_get(&pdev->dev, "pclk");
	if (IS_ERR(xqspi->pclk)) {
		dev_err(&pdev->dev, "pclk clock not found.\n");
		ret = PTR_ERR(xqspi->pclk);
		goto remove_ctlr;
	}

	init_completion(&xqspi->data_completion);

	xqspi->refclk = devm_clk_get(&pdev->dev, "ref_clk");
	if (IS_ERR(xqspi->refclk)) {
		dev_err(&pdev->dev, "ref_clk clock not found.\n");
		ret = PTR_ERR(xqspi->refclk);
		goto remove_ctlr;
	}

	ret = clk_prepare_enable(xqspi->pclk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable APB clock.\n");
		goto remove_ctlr;
	}

	ret = clk_prepare_enable(xqspi->refclk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable device clock.\n");
		goto clk_dis_pclk;
	}

	xqspi->irq = platform_get_irq(pdev, 0);
	if (xqspi->irq < 0) {
		ret = xqspi->irq;
		goto clk_dis_all;
	}
	ret = devm_request_irq(&pdev->dev, xqspi->irq, zynq_qspi_irq,
			       0, pdev->name, xqspi);
	if (ret != 0) {
		ret = -ENXIO;
		dev_err(&pdev->dev, "request_irq failed\n");
		goto clk_dis_all;
	}

	ret = of_property_read_u32(np, "num-cs",
				   &num_cs);
	if (ret < 0) {
		ctlr->num_chipselect = 1;
	} else if (num_cs > ZYNQ_QSPI_MAX_NUM_CS) {
		ret = -EINVAL;
		dev_err(&pdev->dev, "only 2 chip selects are available\n");
		goto clk_dis_all;
	} else {
		ctlr->num_chipselect = num_cs;
	}

	ctlr->mode_bits =  SPI_RX_DUAL | SPI_RX_QUAD |
			    SPI_TX_DUAL | SPI_TX_QUAD;
	ctlr->mem_ops = &zynq_qspi_mem_ops;
	ctlr->setup = zynq_qspi_setup_op;
	ctlr->max_speed_hz = clk_get_rate(xqspi->refclk) / 2;
	ctlr->dev.of_node = np;

	/* QSPI controller initializations */
	zynq_qspi_init_hw(xqspi, ctlr->num_chipselect);

	ret = devm_spi_register_controller(&pdev->dev, ctlr);
	if (ret) {
		dev_err(&pdev->dev, "devm_spi_register_controller failed\n");
		goto clk_dis_all;
	}

	return ret;

clk_dis_all:
	clk_disable_unprepare(xqspi->refclk);
clk_dis_pclk:
	clk_disable_unprepare(xqspi->pclk);
remove_ctlr:
	spi_controller_put(ctlr);

	return ret;
}

/**
 * zynq_qspi_remove - Remove method for the QSPI driver
 * @pdev:	Pointer to the platform_device structure
 *
 * This function is called if a device is physically removed from the system or
 * if the driver module is being unloaded. It frees all resources allocated to
 * the device.
 *
 * Return:	0 on success and error value on failure
 */
static void zynq_qspi_remove(struct platform_device *pdev)
{
	struct zynq_qspi *xqspi = platform_get_drvdata(pdev);

	zynq_qspi_write(xqspi, ZYNQ_QSPI_ENABLE_OFFSET, 0);

	clk_disable_unprepare(xqspi->refclk);
	clk_disable_unprepare(xqspi->pclk);
}

static const struct of_device_id zynq_qspi_of_match[] = {
	{ .compatible = "xlnx,zynq-qspi-1.0", },
	{ /* end of table */ }
};

MODULE_DEVICE_TABLE(of, zynq_qspi_of_match);

/*
 * zynq_qspi_driver - This structure defines the QSPI platform driver
 */
static struct platform_driver zynq_qspi_driver = {
	.probe = zynq_qspi_probe,
	.remove_new = zynq_qspi_remove,
	.driver = {
		.name = "zynq-qspi",
		.of_match_table = zynq_qspi_of_match,
	},
};

module_platform_driver(zynq_qspi_driver);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Xilinx Zynq QSPI driver");
MODULE_LICENSE("GPL");
