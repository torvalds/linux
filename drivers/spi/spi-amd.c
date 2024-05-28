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

#define AMD_SPI_OPCODE_REG	0x45
#define AMD_SPI_CMD_TRIGGER_REG	0x47
#define AMD_SPI_TRIGGER_CMD	BIT(7)

#define AMD_SPI_OPCODE_MASK	0xFF

#define AMD_SPI_ALT_CS_REG	0x1D
#define AMD_SPI_ALT_CS_MASK	0x3

#define AMD_SPI_FIFO_BASE	0x80
#define AMD_SPI_TX_COUNT_REG	0x48
#define AMD_SPI_RX_COUNT_REG	0x4B
#define AMD_SPI_STATUS_REG	0x4C

#define AMD_SPI_FIFO_SIZE	70
#define AMD_SPI_MEM_SIZE	200

#define AMD_SPI_ENA_REG		0x20
#define AMD_SPI_ALT_SPD_SHIFT	20
#define AMD_SPI_ALT_SPD_MASK	GENMASK(23, AMD_SPI_ALT_SPD_SHIFT)
#define AMD_SPI_SPI100_SHIFT	0
#define AMD_SPI_SPI100_MASK	GENMASK(AMD_SPI_SPI100_SHIFT, AMD_SPI_SPI100_SHIFT)
#define AMD_SPI_SPEED_REG	0x6C
#define AMD_SPI_SPD7_SHIFT	8
#define AMD_SPI_SPD7_MASK	GENMASK(13, AMD_SPI_SPD7_SHIFT)

#define AMD_SPI_MAX_HZ		100000000
#define AMD_SPI_MIN_HZ		800000

/**
 * enum amd_spi_versions - SPI controller versions
 * @AMD_SPI_V1:		AMDI0061 hardware version
 * @AMD_SPI_V2:		AMDI0062 hardware version
 */
enum amd_spi_versions {
	AMD_SPI_V1 = 1,
	AMD_SPI_V2,
};

enum amd_spi_speed {
	F_66_66MHz,
	F_33_33MHz,
	F_22_22MHz,
	F_16_66MHz,
	F_100MHz,
	F_800KHz,
	SPI_SPD7 = 0x7,
	F_50MHz = 0x4,
	F_4MHz = 0x32,
	F_3_17MHz = 0x3F
};

/**
 * struct amd_spi_freq - Matches device speed with values to write in regs
 * @speed_hz: Device frequency
 * @enable_val: Value to be written to "enable register"
 * @spd7_val: Some frequencies requires to have a value written at SPISPEED register
 */
struct amd_spi_freq {
	u32 speed_hz;
	u32 enable_val;
	u32 spd7_val;
};

/**
 * struct amd_spi - SPI driver instance
 * @io_remap_addr:	Start address of the SPI controller registers
 * @version:		SPI controller hardware version
 * @speed_hz:		Device frequency
 */
struct amd_spi {
	void __iomem *io_remap_addr;
	enum amd_spi_versions version;
	unsigned int speed_hz;
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

static inline void amd_spi_clear_chip(struct amd_spi *amd_spi, u8 chip_select)
{
	amd_spi_writereg8(amd_spi, AMD_SPI_ALT_CS_REG, chip_select & ~AMD_SPI_ALT_CS_MASK);
}

static void amd_spi_clear_fifo_ptr(struct amd_spi *amd_spi)
{
	amd_spi_setclear_reg32(amd_spi, AMD_SPI_CTRL0_REG, AMD_SPI_FIFO_CLEAR, AMD_SPI_FIFO_CLEAR);
}

static int amd_spi_set_opcode(struct amd_spi *amd_spi, u8 cmd_opcode)
{
	switch (amd_spi->version) {
	case AMD_SPI_V1:
		amd_spi_setclear_reg32(amd_spi, AMD_SPI_CTRL0_REG, cmd_opcode,
				       AMD_SPI_OPCODE_MASK);
		return 0;
	case AMD_SPI_V2:
		amd_spi_writereg8(amd_spi, AMD_SPI_OPCODE_REG, cmd_opcode);
		return 0;
	default:
		return -ENODEV;
	}
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
	int reg;

	switch (amd_spi->version) {
	case AMD_SPI_V1:
		reg = AMD_SPI_CTRL0_REG;
		break;
	case AMD_SPI_V2:
		reg = AMD_SPI_STATUS_REG;
		break;
	default:
		return -ENODEV;
	}

	return readl_poll_timeout(amd_spi->io_remap_addr + reg, val,
				  !(val & AMD_SPI_BUSY), 20, 2000000);
}

static int amd_spi_execute_opcode(struct amd_spi *amd_spi)
{
	int ret;

	ret = amd_spi_busy_wait(amd_spi);
	if (ret)
		return ret;

	switch (amd_spi->version) {
	case AMD_SPI_V1:
		/* Set ExecuteOpCode bit in the CTRL0 register */
		amd_spi_setclear_reg32(amd_spi, AMD_SPI_CTRL0_REG, AMD_SPI_EXEC_CMD,
				       AMD_SPI_EXEC_CMD);
		return 0;
	case AMD_SPI_V2:
		/* Trigger the command execution */
		amd_spi_setclear_reg8(amd_spi, AMD_SPI_CMD_TRIGGER_REG,
				      AMD_SPI_TRIGGER_CMD, AMD_SPI_TRIGGER_CMD);
		return 0;
	default:
		return -ENODEV;
	}
}

static int amd_spi_host_setup(struct spi_device *spi)
{
	struct amd_spi *amd_spi = spi_controller_get_devdata(spi->controller);

	amd_spi_clear_fifo_ptr(amd_spi);

	return 0;
}

static const struct amd_spi_freq amd_spi_freq[] = {
	{ AMD_SPI_MAX_HZ,   F_100MHz,         0},
	{       66660000, F_66_66MHz,         0},
	{       50000000,   SPI_SPD7,   F_50MHz},
	{       33330000, F_33_33MHz,         0},
	{       22220000, F_22_22MHz,         0},
	{       16660000, F_16_66MHz,         0},
	{        4000000,   SPI_SPD7,    F_4MHz},
	{        3170000,   SPI_SPD7, F_3_17MHz},
	{ AMD_SPI_MIN_HZ,   F_800KHz,         0},
};

static int amd_set_spi_freq(struct amd_spi *amd_spi, u32 speed_hz)
{
	unsigned int i, spd7_val, alt_spd;

	if (speed_hz < AMD_SPI_MIN_HZ)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(amd_spi_freq); i++)
		if (speed_hz >= amd_spi_freq[i].speed_hz)
			break;

	if (amd_spi->speed_hz == amd_spi_freq[i].speed_hz)
		return 0;

	amd_spi->speed_hz = amd_spi_freq[i].speed_hz;

	alt_spd = (amd_spi_freq[i].enable_val << AMD_SPI_ALT_SPD_SHIFT)
		   & AMD_SPI_ALT_SPD_MASK;
	amd_spi_setclear_reg32(amd_spi, AMD_SPI_ENA_REG, alt_spd,
			       AMD_SPI_ALT_SPD_MASK);

	if (amd_spi->speed_hz == AMD_SPI_MAX_HZ)
		amd_spi_setclear_reg32(amd_spi, AMD_SPI_ENA_REG, 1,
				       AMD_SPI_SPI100_MASK);

	if (amd_spi_freq[i].spd7_val) {
		spd7_val = (amd_spi_freq[i].spd7_val << AMD_SPI_SPD7_SHIFT)
			    & AMD_SPI_SPD7_MASK;
		amd_spi_setclear_reg32(amd_spi, AMD_SPI_SPEED_REG, spd7_val,
				       AMD_SPI_SPD7_MASK);
	}

	return 0;
}

static inline int amd_spi_fifo_xfer(struct amd_spi *amd_spi,
				    struct spi_controller *host,
				    struct spi_message *message)
{
	struct spi_transfer *xfer = NULL;
	struct spi_device *spi = message->spi;
	u8 cmd_opcode = 0, fifo_pos = AMD_SPI_FIFO_BASE;
	u8 *buf = NULL;
	u32 i = 0;
	u32 tx_len = 0, rx_len = 0;

	list_for_each_entry(xfer, &message->transfers,
			    transfer_list) {
		if (xfer->speed_hz)
			amd_set_spi_freq(amd_spi, xfer->speed_hz);
		else
			amd_set_spi_freq(amd_spi, spi->max_speed_hz);

		if (xfer->tx_buf) {
			buf = (u8 *)xfer->tx_buf;
			if (!tx_len) {
				cmd_opcode = *(u8 *)xfer->tx_buf;
				buf++;
				xfer->len--;
			}
			tx_len += xfer->len;

			/* Write data into the FIFO. */
			for (i = 0; i < xfer->len; i++)
				amd_spi_writereg8(amd_spi, fifo_pos + i, buf[i]);

			fifo_pos += xfer->len;
		}

		/* Store no. of bytes to be received from FIFO */
		if (xfer->rx_buf)
			rx_len += xfer->len;
	}

	if (!buf) {
		message->status = -EINVAL;
		goto fin_msg;
	}

	amd_spi_set_opcode(amd_spi, cmd_opcode);
	amd_spi_set_tx_count(amd_spi, tx_len);
	amd_spi_set_rx_count(amd_spi, rx_len);

	/* Execute command */
	message->status = amd_spi_execute_opcode(amd_spi);
	if (message->status)
		goto fin_msg;

	if (rx_len) {
		message->status = amd_spi_busy_wait(amd_spi);
		if (message->status)
			goto fin_msg;

		list_for_each_entry(xfer, &message->transfers, transfer_list)
			if (xfer->rx_buf) {
				buf = (u8 *)xfer->rx_buf;
				/* Read data from FIFO to receive buffer */
				for (i = 0; i < xfer->len; i++)
					buf[i] = amd_spi_readreg8(amd_spi, fifo_pos + i);
				fifo_pos += xfer->len;
			}
	}

	/* Update statistics */
	message->actual_length = tx_len + rx_len + 1;

fin_msg:
	switch (amd_spi->version) {
	case AMD_SPI_V1:
		break;
	case AMD_SPI_V2:
		amd_spi_clear_chip(amd_spi, spi_get_chipselect(message->spi, 0));
		break;
	default:
		return -ENODEV;
	}

	spi_finalize_current_message(host);

	return message->status;
}

static int amd_spi_host_transfer(struct spi_controller *host,
				   struct spi_message *msg)
{
	struct amd_spi *amd_spi = spi_controller_get_devdata(host);
	struct spi_device *spi = msg->spi;

	amd_spi_select_chip(amd_spi, spi_get_chipselect(spi, 0));

	/*
	 * Extract spi_transfers from the spi message and
	 * program the controller.
	 */
	return amd_spi_fifo_xfer(amd_spi, host, msg);
}

static size_t amd_spi_max_transfer_size(struct spi_device *spi)
{
	return AMD_SPI_FIFO_SIZE;
}

static int amd_spi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct spi_controller *host;
	struct amd_spi *amd_spi;
	int err;

	/* Allocate storage for host and driver private data */
	host = devm_spi_alloc_host(dev, sizeof(struct amd_spi));
	if (!host)
		return dev_err_probe(dev, -ENOMEM, "Error allocating SPI host\n");

	amd_spi = spi_controller_get_devdata(host);
	amd_spi->io_remap_addr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(amd_spi->io_remap_addr))
		return dev_err_probe(dev, PTR_ERR(amd_spi->io_remap_addr),
				     "ioremap of SPI registers failed\n");

	dev_dbg(dev, "io_remap_address: %p\n", amd_spi->io_remap_addr);

	amd_spi->version = (uintptr_t) device_get_match_data(dev);

	/* Initialize the spi_controller fields */
	host->bus_num = 0;
	host->num_chipselect = 4;
	host->mode_bits = 0;
	host->flags = SPI_CONTROLLER_HALF_DUPLEX;
	host->max_speed_hz = AMD_SPI_MAX_HZ;
	host->min_speed_hz = AMD_SPI_MIN_HZ;
	host->setup = amd_spi_host_setup;
	host->transfer_one_message = amd_spi_host_transfer;
	host->max_transfer_size = amd_spi_max_transfer_size;
	host->max_message_size = amd_spi_max_transfer_size;

	/* Register the controller with SPI framework */
	err = devm_spi_register_controller(dev, host);
	if (err)
		return dev_err_probe(dev, err, "error registering SPI controller\n");

	return 0;
}

#ifdef CONFIG_ACPI
static const struct acpi_device_id spi_acpi_match[] = {
	{ "AMDI0061", AMD_SPI_V1 },
	{ "AMDI0062", AMD_SPI_V2 },
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
