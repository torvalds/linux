// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
//
// AMD SPI controller driver
//
// Copyright (c) 2020, Advanced Micro Devices, Inc.
//
// Author: Sanjay R Mehta <sanju.mehta@amd.com>

#include <linux/acpi.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/spi/spi.h>
#include <linux/iopoll.h>

#define AMD_SPI_CTRL0_REG	0x00
#define AMD_SPI_EXEC_CMD	BIT(16)
#define AMD_SPI_FIFO_CLEAR	BIT(20)
#define AMD_SPI_BUSY		BIT(31)

#define AMD_SPI_OPCODE_MASK	0xFF

#define AMD_SPI_ALT_CS_REG	0x1D
#define AMD_SPI_ALT_CS_MASK	0x3

#define AMD_SPI_FIFO_BASE	0x80
#define AMD_SPI_TX_COUNT_REG	0x48
#define AMD_SPI_RX_COUNT_REG	0x4B
#define AMD_SPI_STATUS_REG	0x4C

#define AMD_SPI_MEM_SIZE	200

/* M_CMD OP codes for SPI */
#define AMD_SPI_XFER_TX		1
#define AMD_SPI_XFER_RX		2

struct amd_spi {
	void __iomem *io_remap_addr;
	unsigned long io_base_addr;
};

static inline u8 amd_spi_readreg8(struct amd_spi *amd_spi, int idx)
{
	return ioread8((u8 __iomem *)amd_spi->io_remap_addr + idx);
}

static inline void amd_spi_writereg8(struct amd_spi *amd_spi, int idx, u8 val)
{
	iowrite8(val, ((u8 __iomem *)amd_spi->io_remap_addr + idx));
}

static void amd_spi_setclear_reg8(struct amd_spi *amd_spi, int idx, u8 set, u8 clear)
{
	u8 tmp = amd_spi_readreg8(amd_spi, idx);

	tmp = (tmp & ~clear) | set;
	amd_spi_writereg8(amd_spi, idx, tmp);
}

static inline u32 amd_spi_readreg32(struct amd_spi *amd_spi, int idx)
{
	return ioread32((u8 __iomem *)amd_spi->io_remap_addr + idx);
}

static inline void amd_spi_writereg32(struct amd_spi *amd_spi, int idx, u32 val)
{
	iowrite32(val, ((u8 __iomem *)amd_spi->io_remap_addr + idx));
}

static inline void amd_spi_setclear_reg32(struct amd_spi *amd_spi, int idx, u32 set, u32 clear)
{
	u32 tmp = amd_spi_readreg32(amd_spi, idx);

	tmp = (tmp & ~clear) | set;
	amd_spi_writereg32(amd_spi, idx, tmp);
}

static void amd_spi_select_chip(struct amd_spi *amd_spi, u8 cs)
{
	amd_spi_setclear_reg8(amd_spi, AMD_SPI_ALT_CS_REG, cs, AMD_SPI_ALT_CS_MASK);
}

static void amd_spi_clear_fifo_ptr(struct amd_spi *amd_spi)
{
	amd_spi_setclear_reg32(amd_spi, AMD_SPI_CTRL0_REG, AMD_SPI_FIFO_CLEAR, AMD_SPI_FIFO_CLEAR);
}

static void amd_spi_set_opcode(struct amd_spi *amd_spi, u8 cmd_opcode)
{
	amd_spi_setclear_reg32(amd_spi, AMD_SPI_CTRL0_REG, cmd_opcode, AMD_SPI_OPCODE_MASK);
}

static inline void amd_spi_set_rx_count(struct amd_spi *amd_spi, u8 rx_count)
{
	amd_spi_setclear_reg8(amd_spi, AMD_SPI_RX_COUNT_REG, rx_count, 0xff);
}

static inline void amd_spi_set_tx_count(struct amd_spi *amd_spi, u8 tx_count)
{
	amd_spi_setclear_reg8(amd_spi, AMD_SPI_TX_COUNT_REG, tx_count, 0xff);
}

static int amd_spi_busy_wait(struct amd_spi *amd_spi)
{
	u32 val;

	return readl_poll_timeout(amd_spi->io_remap_addr + AMD_SPI_CTRL0_REG,
				  val, !(val & AMD_SPI_BUSY), 20, 2000000);
}

static int amd_spi_execute_opcode(struct amd_spi *amd_spi)
{
	int ret;

	ret = amd_spi_busy_wait(amd_spi);
	if (ret)
		return ret;

	/* Set ExecuteOpCode bit in the CTRL0 register */
	amd_spi_setclear_reg32(amd_spi, AMD_SPI_CTRL0_REG, AMD_SPI_EXEC_CMD, AMD_SPI_EXEC_CMD);

	return 0;
}

static int amd_spi_master_setup(struct spi_device *spi)
{
	struct amd_spi *amd_spi = spi_master_get_devdata(spi->master);

	amd_spi_clear_fifo_ptr(amd_spi);

	return 0;
}

static inline int amd_spi_fifo_xfer(struct amd_spi *amd_spi,
				    struct spi_master *master,
				    struct spi_message *message)
{
	struct spi_transfer *xfer = NULL;
	u8 cmd_opcode;
	u8 *buf = NULL;
	u32 m_cmd = 0;
	u32 i = 0;
	u32 tx_len = 0, rx_len = 0;

	list_for_each_entry(xfer, &message->transfers,
			    transfer_list) {
		if (xfer->rx_buf)
			m_cmd = AMD_SPI_XFER_RX;
		if (xfer->tx_buf)
			m_cmd = AMD_SPI_XFER_TX;

		if (m_cmd & AMD_SPI_XFER_TX) {
			buf = (u8 *)xfer->tx_buf;
			tx_len = xfer->len - 1;
			cmd_opcode = *(u8 *)xfer->tx_buf;
			buf++;
			amd_spi_set_opcode(amd_spi, cmd_opcode);

			/* Write data into the FIFO. */
			for (i = 0; i < tx_len; i++) {
				iowrite8(buf[i], ((u8 __iomem *)amd_spi->io_remap_addr +
					 AMD_SPI_FIFO_BASE + i));
			}

			amd_spi_set_tx_count(amd_spi, tx_len);
			amd_spi_clear_fifo_ptr(amd_spi);
			/* Execute command */
			amd_spi_execute_opcode(amd_spi);
		}
		if (m_cmd & AMD_SPI_XFER_RX) {
			/*
			 * Store no. of bytes to be received from
			 * FIFO
			 */
			rx_len = xfer->len;
			buf = (u8 *)xfer->rx_buf;
			amd_spi_set_rx_count(amd_spi, rx_len);
			amd_spi_clear_fifo_ptr(amd_spi);
			/* Execute command */
			amd_spi_execute_opcode(amd_spi);
			amd_spi_busy_wait(amd_spi);
			/* Read data from FIFO to receive buffer  */
			for (i = 0; i < rx_len; i++)
				buf[i] = amd_spi_readreg8(amd_spi, AMD_SPI_FIFO_BASE + tx_len + i);
		}
	}

	/* Update statistics */
	message->actual_length = tx_len + rx_len + 1;
	/* complete the transaction */
	message->status = 0;
	spi_finalize_current_message(master);

	return 0;
}

static int amd_spi_master_transfer(struct spi_master *master,
				   struct spi_message *msg)
{
	struct amd_spi *amd_spi = spi_master_get_devdata(master);
	struct spi_device *spi = msg->spi;

	amd_spi_select_chip(amd_spi, spi->chip_select);

	/*
	 * Extract spi_transfers from the spi message and
	 * program the controller.
	 */
	amd_spi_fifo_xfer(amd_spi, master, msg);

	return 0;
}

static int amd_spi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct spi_master *master;
	struct amd_spi *amd_spi;
	int err = 0;

	/* Allocate storage for spi_master and driver private data */
	master = spi_alloc_master(dev, sizeof(struct amd_spi));
	if (!master) {
		dev_err(dev, "Error allocating SPI master\n");
		return -ENOMEM;
	}

	amd_spi = spi_master_get_devdata(master);
	amd_spi->io_remap_addr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(amd_spi->io_remap_addr)) {
		err = PTR_ERR(amd_spi->io_remap_addr);
		dev_err(dev, "error %d ioremap of SPI registers failed\n", err);
		goto err_free_master;
	}
	dev_dbg(dev, "io_remap_address: %p\n", amd_spi->io_remap_addr);

	/* Initialize the spi_master fields */
	master->bus_num = 0;
	master->num_chipselect = 4;
	master->mode_bits = 0;
	master->flags = SPI_MASTER_HALF_DUPLEX;
	master->setup = amd_spi_master_setup;
	master->transfer_one_message = amd_spi_master_transfer;

	/* Register the controller with SPI framework */
	err = devm_spi_register_master(dev, master);
	if (err) {
		dev_err(dev, "error %d registering SPI controller\n", err);
		goto err_free_master;
	}

	return 0;

err_free_master:
	spi_master_put(master);

	return err;
}

#ifdef CONFIG_ACPI
static const struct acpi_device_id spi_acpi_match[] = {
	{ "AMDI0061", 0 },
	{},
};
MODULE_DEVICE_TABLE(acpi, spi_acpi_match);
#endif

static struct platform_driver amd_spi_driver = {
	.driver = {
		.name = "amd_spi",
		.acpi_match_table = ACPI_PTR(spi_acpi_match),
	},
	.probe = amd_spi_probe,
};

module_platform_driver(amd_spi_driver);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Sanjay Mehta <sanju.mehta@amd.com>");
MODULE_DESCRIPTION("AMD SPI Master Controller Driver");
