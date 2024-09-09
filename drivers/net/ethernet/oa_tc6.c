// SPDX-License-Identifier: GPL-2.0+
/*
 * OPEN Alliance 10BASE‑T1x MAC‑PHY Serial Interface framework
 *
 * Author: Parthiban Veerasooran <parthiban.veerasooran@microchip.com>
 */

#include <linux/bitfield.h>
#include <linux/iopoll.h>
#include <linux/oa_tc6.h>

/* OPEN Alliance TC6 registers */
/* Reset Control and Status Register */
#define OA_TC6_REG_RESET			0x0003
#define RESET_SWRESET				BIT(0)	/* Software Reset */

/* Status Register #0 */
#define OA_TC6_REG_STATUS0			0x0008
#define STATUS0_RESETC				BIT(6)	/* Reset Complete */

/* Interrupt Mask Register #0 */
#define OA_TC6_REG_INT_MASK0			0x000C
#define INT_MASK0_HEADER_ERR_MASK		BIT(5)
#define INT_MASK0_LOSS_OF_FRAME_ERR_MASK	BIT(4)
#define INT_MASK0_RX_BUFFER_OVERFLOW_ERR_MASK	BIT(3)
#define INT_MASK0_TX_PROTOCOL_ERR_MASK		BIT(0)

/* Control command header */
#define OA_TC6_CTRL_HEADER_DATA_NOT_CTRL	BIT(31)
#define OA_TC6_CTRL_HEADER_WRITE_NOT_READ	BIT(29)
#define OA_TC6_CTRL_HEADER_MEM_MAP_SELECTOR	GENMASK(27, 24)
#define OA_TC6_CTRL_HEADER_ADDR			GENMASK(23, 8)
#define OA_TC6_CTRL_HEADER_LENGTH		GENMASK(7, 1)
#define OA_TC6_CTRL_HEADER_PARITY		BIT(0)

#define OA_TC6_CTRL_HEADER_SIZE			4
#define OA_TC6_CTRL_REG_VALUE_SIZE		4
#define OA_TC6_CTRL_IGNORED_SIZE		4
#define OA_TC6_CTRL_MAX_REGISTERS		128
#define OA_TC6_CTRL_SPI_BUF_SIZE		(OA_TC6_CTRL_HEADER_SIZE +\
						(OA_TC6_CTRL_MAX_REGISTERS *\
						OA_TC6_CTRL_REG_VALUE_SIZE) +\
						OA_TC6_CTRL_IGNORED_SIZE)
#define STATUS0_RESETC_POLL_DELAY		1000
#define STATUS0_RESETC_POLL_TIMEOUT		1000000

/* Internal structure for MAC-PHY drivers */
struct oa_tc6 {
	struct spi_device *spi;
	struct mutex spi_ctrl_lock; /* Protects spi control transfer */
	void *spi_ctrl_tx_buf;
	void *spi_ctrl_rx_buf;
};

enum oa_tc6_header_type {
	OA_TC6_CTRL_HEADER,
};

enum oa_tc6_register_op {
	OA_TC6_CTRL_REG_READ = 0,
	OA_TC6_CTRL_REG_WRITE = 1,
};

static int oa_tc6_spi_transfer(struct oa_tc6 *tc6,
			       enum oa_tc6_header_type header_type, u16 length)
{
	struct spi_transfer xfer = { 0 };
	struct spi_message msg;

	xfer.tx_buf = tc6->spi_ctrl_tx_buf;
	xfer.rx_buf = tc6->spi_ctrl_rx_buf;
	xfer.len = length;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	return spi_sync(tc6->spi, &msg);
}

static int oa_tc6_get_parity(u32 p)
{
	/* Public domain code snippet, lifted from
	 * http://www-graphics.stanford.edu/~seander/bithacks.html
	 */
	p ^= p >> 1;
	p ^= p >> 2;
	p = (p & 0x11111111U) * 0x11111111U;

	/* Odd parity is used here */
	return !((p >> 28) & 1);
}

static __be32 oa_tc6_prepare_ctrl_header(u32 addr, u8 length,
					 enum oa_tc6_register_op reg_op)
{
	u32 header;

	header = FIELD_PREP(OA_TC6_CTRL_HEADER_DATA_NOT_CTRL,
			    OA_TC6_CTRL_HEADER) |
		 FIELD_PREP(OA_TC6_CTRL_HEADER_WRITE_NOT_READ, reg_op) |
		 FIELD_PREP(OA_TC6_CTRL_HEADER_MEM_MAP_SELECTOR, addr >> 16) |
		 FIELD_PREP(OA_TC6_CTRL_HEADER_ADDR, addr) |
		 FIELD_PREP(OA_TC6_CTRL_HEADER_LENGTH, length - 1);
	header |= FIELD_PREP(OA_TC6_CTRL_HEADER_PARITY,
			     oa_tc6_get_parity(header));

	return cpu_to_be32(header);
}

static void oa_tc6_update_ctrl_write_data(struct oa_tc6 *tc6, u32 value[],
					  u8 length)
{
	__be32 *tx_buf = tc6->spi_ctrl_tx_buf + OA_TC6_CTRL_HEADER_SIZE;

	for (int i = 0; i < length; i++)
		*tx_buf++ = cpu_to_be32(value[i]);
}

static u16 oa_tc6_calculate_ctrl_buf_size(u8 length)
{
	/* Control command consists 4 bytes header + 4 bytes register value for
	 * each register + 4 bytes ignored value.
	 */
	return OA_TC6_CTRL_HEADER_SIZE + OA_TC6_CTRL_REG_VALUE_SIZE * length +
	       OA_TC6_CTRL_IGNORED_SIZE;
}

static void oa_tc6_prepare_ctrl_spi_buf(struct oa_tc6 *tc6, u32 address,
					u32 value[], u8 length,
					enum oa_tc6_register_op reg_op)
{
	__be32 *tx_buf = tc6->spi_ctrl_tx_buf;

	*tx_buf = oa_tc6_prepare_ctrl_header(address, length, reg_op);

	if (reg_op == OA_TC6_CTRL_REG_WRITE)
		oa_tc6_update_ctrl_write_data(tc6, value, length);
}

static int oa_tc6_check_ctrl_write_reply(struct oa_tc6 *tc6, u8 size)
{
	u8 *tx_buf = tc6->spi_ctrl_tx_buf;
	u8 *rx_buf = tc6->spi_ctrl_rx_buf;

	rx_buf += OA_TC6_CTRL_IGNORED_SIZE;

	/* The echoed control write must match with the one that was
	 * transmitted.
	 */
	if (memcmp(tx_buf, rx_buf, size - OA_TC6_CTRL_IGNORED_SIZE))
		return -EPROTO;

	return 0;
}

static int oa_tc6_check_ctrl_read_reply(struct oa_tc6 *tc6, u8 size)
{
	u32 *rx_buf = tc6->spi_ctrl_rx_buf + OA_TC6_CTRL_IGNORED_SIZE;
	u32 *tx_buf = tc6->spi_ctrl_tx_buf;

	/* The echoed control read header must match with the one that was
	 * transmitted.
	 */
	if (*tx_buf != *rx_buf)
		return -EPROTO;

	return 0;
}

static void oa_tc6_copy_ctrl_read_data(struct oa_tc6 *tc6, u32 value[],
				       u8 length)
{
	__be32 *rx_buf = tc6->spi_ctrl_rx_buf + OA_TC6_CTRL_IGNORED_SIZE +
			 OA_TC6_CTRL_HEADER_SIZE;

	for (int i = 0; i < length; i++)
		value[i] = be32_to_cpu(*rx_buf++);
}

static int oa_tc6_perform_ctrl(struct oa_tc6 *tc6, u32 address, u32 value[],
			       u8 length, enum oa_tc6_register_op reg_op)
{
	u16 size;
	int ret;

	/* Prepare control command and copy to SPI control buffer */
	oa_tc6_prepare_ctrl_spi_buf(tc6, address, value, length, reg_op);

	size = oa_tc6_calculate_ctrl_buf_size(length);

	/* Perform SPI transfer */
	ret = oa_tc6_spi_transfer(tc6, OA_TC6_CTRL_HEADER, size);
	if (ret) {
		dev_err(&tc6->spi->dev, "SPI transfer failed for control: %d\n",
			ret);
		return ret;
	}

	/* Check echoed/received control write command reply for errors */
	if (reg_op == OA_TC6_CTRL_REG_WRITE)
		return oa_tc6_check_ctrl_write_reply(tc6, size);

	/* Check echoed/received control read command reply for errors */
	ret = oa_tc6_check_ctrl_read_reply(tc6, size);
	if (ret)
		return ret;

	oa_tc6_copy_ctrl_read_data(tc6, value, length);

	return 0;
}

/**
 * oa_tc6_read_registers - function for reading multiple consecutive registers.
 * @tc6: oa_tc6 struct.
 * @address: address of the first register to be read in the MAC-PHY.
 * @value: values to be read from the starting register address @address.
 * @length: number of consecutive registers to be read from @address.
 *
 * Maximum of 128 consecutive registers can be read starting at @address.
 *
 * Return: 0 on success otherwise failed.
 */
int oa_tc6_read_registers(struct oa_tc6 *tc6, u32 address, u32 value[],
			  u8 length)
{
	int ret;

	if (!length || length > OA_TC6_CTRL_MAX_REGISTERS) {
		dev_err(&tc6->spi->dev, "Invalid register length parameter\n");
		return -EINVAL;
	}

	mutex_lock(&tc6->spi_ctrl_lock);
	ret = oa_tc6_perform_ctrl(tc6, address, value, length,
				  OA_TC6_CTRL_REG_READ);
	mutex_unlock(&tc6->spi_ctrl_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(oa_tc6_read_registers);

/**
 * oa_tc6_read_register - function for reading a MAC-PHY register.
 * @tc6: oa_tc6 struct.
 * @address: register address of the MAC-PHY to be read.
 * @value: value read from the @address register address of the MAC-PHY.
 *
 * Return: 0 on success otherwise failed.
 */
int oa_tc6_read_register(struct oa_tc6 *tc6, u32 address, u32 *value)
{
	return oa_tc6_read_registers(tc6, address, value, 1);
}
EXPORT_SYMBOL_GPL(oa_tc6_read_register);

/**
 * oa_tc6_write_registers - function for writing multiple consecutive registers.
 * @tc6: oa_tc6 struct.
 * @address: address of the first register to be written in the MAC-PHY.
 * @value: values to be written from the starting register address @address.
 * @length: number of consecutive registers to be written from @address.
 *
 * Maximum of 128 consecutive registers can be written starting at @address.
 *
 * Return: 0 on success otherwise failed.
 */
int oa_tc6_write_registers(struct oa_tc6 *tc6, u32 address, u32 value[],
			   u8 length)
{
	int ret;

	if (!length || length > OA_TC6_CTRL_MAX_REGISTERS) {
		dev_err(&tc6->spi->dev, "Invalid register length parameter\n");
		return -EINVAL;
	}

	mutex_lock(&tc6->spi_ctrl_lock);
	ret = oa_tc6_perform_ctrl(tc6, address, value, length,
				  OA_TC6_CTRL_REG_WRITE);
	mutex_unlock(&tc6->spi_ctrl_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(oa_tc6_write_registers);

/**
 * oa_tc6_write_register - function for writing a MAC-PHY register.
 * @tc6: oa_tc6 struct.
 * @address: register address of the MAC-PHY to be written.
 * @value: value to be written in the @address register address of the MAC-PHY.
 *
 * Return: 0 on success otherwise failed.
 */
int oa_tc6_write_register(struct oa_tc6 *tc6, u32 address, u32 value)
{
	return oa_tc6_write_registers(tc6, address, &value, 1);
}
EXPORT_SYMBOL_GPL(oa_tc6_write_register);

static int oa_tc6_read_status0(struct oa_tc6 *tc6)
{
	u32 regval;
	int ret;

	ret = oa_tc6_read_register(tc6, OA_TC6_REG_STATUS0, &regval);
	if (ret) {
		dev_err(&tc6->spi->dev, "STATUS0 register read failed: %d\n",
			ret);
		return 0;
	}

	return regval;
}

static int oa_tc6_sw_reset_macphy(struct oa_tc6 *tc6)
{
	u32 regval = RESET_SWRESET;
	int ret;

	ret = oa_tc6_write_register(tc6, OA_TC6_REG_RESET, regval);
	if (ret)
		return ret;

	/* Poll for soft reset complete for every 1ms until 1s timeout */
	ret = readx_poll_timeout(oa_tc6_read_status0, tc6, regval,
				 regval & STATUS0_RESETC,
				 STATUS0_RESETC_POLL_DELAY,
				 STATUS0_RESETC_POLL_TIMEOUT);
	if (ret)
		return -ENODEV;

	/* Clear the reset complete status */
	return oa_tc6_write_register(tc6, OA_TC6_REG_STATUS0, regval);
}

static int oa_tc6_unmask_macphy_error_interrupts(struct oa_tc6 *tc6)
{
	u32 regval;
	int ret;

	ret = oa_tc6_read_register(tc6, OA_TC6_REG_INT_MASK0, &regval);
	if (ret)
		return ret;

	regval &= ~(INT_MASK0_TX_PROTOCOL_ERR_MASK |
		    INT_MASK0_RX_BUFFER_OVERFLOW_ERR_MASK |
		    INT_MASK0_LOSS_OF_FRAME_ERR_MASK |
		    INT_MASK0_HEADER_ERR_MASK);

	return oa_tc6_write_register(tc6, OA_TC6_REG_INT_MASK0, regval);
}

/**
 * oa_tc6_init - allocates and initializes oa_tc6 structure.
 * @spi: device with which data will be exchanged.
 *
 * Return: pointer reference to the oa_tc6 structure if the MAC-PHY
 * initialization is successful otherwise NULL.
 */
struct oa_tc6 *oa_tc6_init(struct spi_device *spi)
{
	struct oa_tc6 *tc6;
	int ret;

	tc6 = devm_kzalloc(&spi->dev, sizeof(*tc6), GFP_KERNEL);
	if (!tc6)
		return NULL;

	tc6->spi = spi;
	mutex_init(&tc6->spi_ctrl_lock);

	/* Set the SPI controller to pump at realtime priority */
	tc6->spi->rt = true;
	spi_setup(tc6->spi);

	tc6->spi_ctrl_tx_buf = devm_kzalloc(&tc6->spi->dev,
					    OA_TC6_CTRL_SPI_BUF_SIZE,
					    GFP_KERNEL);
	if (!tc6->spi_ctrl_tx_buf)
		return NULL;

	tc6->spi_ctrl_rx_buf = devm_kzalloc(&tc6->spi->dev,
					    OA_TC6_CTRL_SPI_BUF_SIZE,
					    GFP_KERNEL);
	if (!tc6->spi_ctrl_rx_buf)
		return NULL;

	ret = oa_tc6_sw_reset_macphy(tc6);
	if (ret) {
		dev_err(&tc6->spi->dev,
			"MAC-PHY software reset failed: %d\n", ret);
		return NULL;
	}

	ret = oa_tc6_unmask_macphy_error_interrupts(tc6);
	if (ret) {
		dev_err(&tc6->spi->dev,
			"MAC-PHY error interrupts unmask failed: %d\n", ret);
		return NULL;
	}

	return tc6;
}
EXPORT_SYMBOL_GPL(oa_tc6_init);

MODULE_DESCRIPTION("OPEN Alliance 10BASE‑T1x MAC‑PHY Serial Interface Lib");
MODULE_AUTHOR("Parthiban Veerasooran <parthiban.veerasooran@microchip.com>");
MODULE_LICENSE("GPL");
