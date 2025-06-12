// SPDX-License-Identifier: GPL-2.0+
/*
 * OPEN Alliance 10BASE‑T1x MAC‑PHY Serial Interface framework
 *
 * Author: Parthiban Veerasooran <parthiban.veerasooran@microchip.com>
 */

#include <linux/bitfield.h>
#include <linux/iopoll.h>
#include <linux/mdio.h>
#include <linux/phy.h>
#include <linux/oa_tc6.h>

/* OPEN Alliance TC6 registers */
/* Standard Capabilities Register */
#define OA_TC6_REG_STDCAP			0x0002
#define STDCAP_DIRECT_PHY_REG_ACCESS		BIT(8)

/* Reset Control and Status Register */
#define OA_TC6_REG_RESET			0x0003
#define RESET_SWRESET				BIT(0)	/* Software Reset */

/* Configuration Register #0 */
#define OA_TC6_REG_CONFIG0			0x0004
#define CONFIG0_SYNC				BIT(15)
#define CONFIG0_ZARFE_ENABLE			BIT(12)

/* Status Register #0 */
#define OA_TC6_REG_STATUS0			0x0008
#define STATUS0_RESETC				BIT(6)	/* Reset Complete */
#define STATUS0_HEADER_ERROR			BIT(5)
#define STATUS0_LOSS_OF_FRAME_ERROR		BIT(4)
#define STATUS0_RX_BUFFER_OVERFLOW_ERROR	BIT(3)
#define STATUS0_TX_PROTOCOL_ERROR		BIT(0)

/* Buffer Status Register */
#define OA_TC6_REG_BUFFER_STATUS		0x000B
#define BUFFER_STATUS_TX_CREDITS_AVAILABLE	GENMASK(15, 8)
#define BUFFER_STATUS_RX_CHUNKS_AVAILABLE	GENMASK(7, 0)

/* Interrupt Mask Register #0 */
#define OA_TC6_REG_INT_MASK0			0x000C
#define INT_MASK0_HEADER_ERR_MASK		BIT(5)
#define INT_MASK0_LOSS_OF_FRAME_ERR_MASK	BIT(4)
#define INT_MASK0_RX_BUFFER_OVERFLOW_ERR_MASK	BIT(3)
#define INT_MASK0_TX_PROTOCOL_ERR_MASK		BIT(0)

/* PHY Clause 22 registers base address and mask */
#define OA_TC6_PHY_STD_REG_ADDR_BASE		0xFF00
#define OA_TC6_PHY_STD_REG_ADDR_MASK		0x1F

/* Control command header */
#define OA_TC6_CTRL_HEADER_DATA_NOT_CTRL	BIT(31)
#define OA_TC6_CTRL_HEADER_WRITE_NOT_READ	BIT(29)
#define OA_TC6_CTRL_HEADER_MEM_MAP_SELECTOR	GENMASK(27, 24)
#define OA_TC6_CTRL_HEADER_ADDR			GENMASK(23, 8)
#define OA_TC6_CTRL_HEADER_LENGTH		GENMASK(7, 1)
#define OA_TC6_CTRL_HEADER_PARITY		BIT(0)

/* Data header */
#define OA_TC6_DATA_HEADER_DATA_NOT_CTRL	BIT(31)
#define OA_TC6_DATA_HEADER_DATA_VALID		BIT(21)
#define OA_TC6_DATA_HEADER_START_VALID		BIT(20)
#define OA_TC6_DATA_HEADER_START_WORD_OFFSET	GENMASK(19, 16)
#define OA_TC6_DATA_HEADER_END_VALID		BIT(14)
#define OA_TC6_DATA_HEADER_END_BYTE_OFFSET	GENMASK(13, 8)
#define OA_TC6_DATA_HEADER_PARITY		BIT(0)

/* Data footer */
#define OA_TC6_DATA_FOOTER_EXTENDED_STS		BIT(31)
#define OA_TC6_DATA_FOOTER_RXD_HEADER_BAD	BIT(30)
#define OA_TC6_DATA_FOOTER_CONFIG_SYNC		BIT(29)
#define OA_TC6_DATA_FOOTER_RX_CHUNKS		GENMASK(28, 24)
#define OA_TC6_DATA_FOOTER_DATA_VALID		BIT(21)
#define OA_TC6_DATA_FOOTER_START_VALID		BIT(20)
#define OA_TC6_DATA_FOOTER_START_WORD_OFFSET	GENMASK(19, 16)
#define OA_TC6_DATA_FOOTER_END_VALID		BIT(14)
#define OA_TC6_DATA_FOOTER_END_BYTE_OFFSET	GENMASK(13, 8)
#define OA_TC6_DATA_FOOTER_TX_CREDITS		GENMASK(5, 1)

/* PHY – Clause 45 registers memory map selector (MMS) as per table 6 in the
 * OPEN Alliance specification.
 */
#define OA_TC6_PHY_C45_PCS_MMS2			2	/* MMD 3 */
#define OA_TC6_PHY_C45_PMA_PMD_MMS3		3	/* MMD 1 */
#define OA_TC6_PHY_C45_VS_PLCA_MMS4		4	/* MMD 31 */
#define OA_TC6_PHY_C45_AUTO_NEG_MMS5		5	/* MMD 7 */
#define OA_TC6_PHY_C45_POWER_UNIT_MMS6		6	/* MMD 13 */

#define OA_TC6_CTRL_HEADER_SIZE			4
#define OA_TC6_CTRL_REG_VALUE_SIZE		4
#define OA_TC6_CTRL_IGNORED_SIZE		4
#define OA_TC6_CTRL_MAX_REGISTERS		128
#define OA_TC6_CTRL_SPI_BUF_SIZE		(OA_TC6_CTRL_HEADER_SIZE +\
						(OA_TC6_CTRL_MAX_REGISTERS *\
						OA_TC6_CTRL_REG_VALUE_SIZE) +\
						OA_TC6_CTRL_IGNORED_SIZE)
#define OA_TC6_CHUNK_PAYLOAD_SIZE		64
#define OA_TC6_DATA_HEADER_SIZE			4
#define OA_TC6_CHUNK_SIZE			(OA_TC6_DATA_HEADER_SIZE +\
						OA_TC6_CHUNK_PAYLOAD_SIZE)
#define OA_TC6_MAX_TX_CHUNKS			48
#define OA_TC6_SPI_DATA_BUF_SIZE		(OA_TC6_MAX_TX_CHUNKS *\
						OA_TC6_CHUNK_SIZE)
#define STATUS0_RESETC_POLL_DELAY		1000
#define STATUS0_RESETC_POLL_TIMEOUT		1000000

/* Internal structure for MAC-PHY drivers */
struct oa_tc6 {
	struct device *dev;
	struct net_device *netdev;
	struct phy_device *phydev;
	struct mii_bus *mdiobus;
	struct spi_device *spi;
	struct mutex spi_ctrl_lock; /* Protects spi control transfer */
	spinlock_t tx_skb_lock; /* Protects tx skb handling */
	void *spi_ctrl_tx_buf;
	void *spi_ctrl_rx_buf;
	void *spi_data_tx_buf;
	void *spi_data_rx_buf;
	struct sk_buff *ongoing_tx_skb;
	struct sk_buff *waiting_tx_skb;
	struct sk_buff *rx_skb;
	struct task_struct *spi_thread;
	wait_queue_head_t spi_wq;
	u16 tx_skb_offset;
	u16 spi_data_tx_buf_offset;
	u16 tx_credits;
	u8 rx_chunks_available;
	bool rx_buf_overflow;
	bool int_flag;
};

enum oa_tc6_header_type {
	OA_TC6_CTRL_HEADER,
	OA_TC6_DATA_HEADER,
};

enum oa_tc6_register_op {
	OA_TC6_CTRL_REG_READ = 0,
	OA_TC6_CTRL_REG_WRITE = 1,
};

enum oa_tc6_data_valid_info {
	OA_TC6_DATA_INVALID,
	OA_TC6_DATA_VALID,
};

enum oa_tc6_data_start_valid_info {
	OA_TC6_DATA_START_INVALID,
	OA_TC6_DATA_START_VALID,
};

enum oa_tc6_data_end_valid_info {
	OA_TC6_DATA_END_INVALID,
	OA_TC6_DATA_END_VALID,
};

static int oa_tc6_spi_transfer(struct oa_tc6 *tc6,
			       enum oa_tc6_header_type header_type, u16 length)
{
	struct spi_transfer xfer = { 0 };
	struct spi_message msg;

	if (header_type == OA_TC6_DATA_HEADER) {
		xfer.tx_buf = tc6->spi_data_tx_buf;
		xfer.rx_buf = tc6->spi_data_rx_buf;
	} else {
		xfer.tx_buf = tc6->spi_ctrl_tx_buf;
		xfer.rx_buf = tc6->spi_ctrl_rx_buf;
	}
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

static int oa_tc6_check_phy_reg_direct_access_capability(struct oa_tc6 *tc6)
{
	u32 regval;
	int ret;

	ret = oa_tc6_read_register(tc6, OA_TC6_REG_STDCAP, &regval);
	if (ret)
		return ret;

	if (!(regval & STDCAP_DIRECT_PHY_REG_ACCESS))
		return -ENODEV;

	return 0;
}

static void oa_tc6_handle_link_change(struct net_device *netdev)
{
	phy_print_status(netdev->phydev);
}

static int oa_tc6_mdiobus_read(struct mii_bus *bus, int addr, int regnum)
{
	struct oa_tc6 *tc6 = bus->priv;
	u32 regval;
	bool ret;

	ret = oa_tc6_read_register(tc6, OA_TC6_PHY_STD_REG_ADDR_BASE |
				   (regnum & OA_TC6_PHY_STD_REG_ADDR_MASK),
				   &regval);
	if (ret)
		return ret;

	return regval;
}

static int oa_tc6_mdiobus_write(struct mii_bus *bus, int addr, int regnum,
				u16 val)
{
	struct oa_tc6 *tc6 = bus->priv;

	return oa_tc6_write_register(tc6, OA_TC6_PHY_STD_REG_ADDR_BASE |
				     (regnum & OA_TC6_PHY_STD_REG_ADDR_MASK),
				     val);
}

static int oa_tc6_get_phy_c45_mms(int devnum)
{
	switch (devnum) {
	case MDIO_MMD_PCS:
		return OA_TC6_PHY_C45_PCS_MMS2;
	case MDIO_MMD_PMAPMD:
		return OA_TC6_PHY_C45_PMA_PMD_MMS3;
	case MDIO_MMD_VEND2:
		return OA_TC6_PHY_C45_VS_PLCA_MMS4;
	case MDIO_MMD_AN:
		return OA_TC6_PHY_C45_AUTO_NEG_MMS5;
	case MDIO_MMD_POWER_UNIT:
		return OA_TC6_PHY_C45_POWER_UNIT_MMS6;
	default:
		return -EOPNOTSUPP;
	}
}

static int oa_tc6_mdiobus_read_c45(struct mii_bus *bus, int addr, int devnum,
				   int regnum)
{
	struct oa_tc6 *tc6 = bus->priv;
	u32 regval;
	int ret;

	ret = oa_tc6_get_phy_c45_mms(devnum);
	if (ret < 0)
		return ret;

	ret = oa_tc6_read_register(tc6, (ret << 16) | regnum, &regval);
	if (ret)
		return ret;

	return regval;
}

static int oa_tc6_mdiobus_write_c45(struct mii_bus *bus, int addr, int devnum,
				    int regnum, u16 val)
{
	struct oa_tc6 *tc6 = bus->priv;
	int ret;

	ret = oa_tc6_get_phy_c45_mms(devnum);
	if (ret < 0)
		return ret;

	return oa_tc6_write_register(tc6, (ret << 16) | regnum, val);
}

static int oa_tc6_mdiobus_register(struct oa_tc6 *tc6)
{
	int ret;

	tc6->mdiobus = mdiobus_alloc();
	if (!tc6->mdiobus) {
		netdev_err(tc6->netdev, "MDIO bus alloc failed\n");
		return -ENOMEM;
	}

	tc6->mdiobus->priv = tc6;
	tc6->mdiobus->read = oa_tc6_mdiobus_read;
	tc6->mdiobus->write = oa_tc6_mdiobus_write;
	/* OPEN Alliance 10BASE-T1x compliance MAC-PHYs will have both C22 and
	 * C45 registers space. If the PHY is discovered via C22 bus protocol it
	 * assumes it uses C22 protocol and always uses C22 registers indirect
	 * access to access C45 registers. This is because, we don't have a
	 * clean separation between C22/C45 register space and C22/C45 MDIO bus
	 * protocols. Resulting, PHY C45 registers direct access can't be used
	 * which can save multiple SPI bus access. To support this feature, PHY
	 * drivers can set .read_mmd/.write_mmd in the PHY driver to call
	 * .read_c45/.write_c45. Ex: drivers/net/phy/microchip_t1s.c
	 */
	tc6->mdiobus->read_c45 = oa_tc6_mdiobus_read_c45;
	tc6->mdiobus->write_c45 = oa_tc6_mdiobus_write_c45;
	tc6->mdiobus->name = "oa-tc6-mdiobus";
	tc6->mdiobus->parent = tc6->dev;

	snprintf(tc6->mdiobus->id, ARRAY_SIZE(tc6->mdiobus->id), "%s",
		 dev_name(&tc6->spi->dev));

	ret = mdiobus_register(tc6->mdiobus);
	if (ret) {
		netdev_err(tc6->netdev, "Could not register MDIO bus\n");
		mdiobus_free(tc6->mdiobus);
		return ret;
	}

	return 0;
}

static void oa_tc6_mdiobus_unregister(struct oa_tc6 *tc6)
{
	mdiobus_unregister(tc6->mdiobus);
	mdiobus_free(tc6->mdiobus);
}

static int oa_tc6_phy_init(struct oa_tc6 *tc6)
{
	int ret;

	ret = oa_tc6_check_phy_reg_direct_access_capability(tc6);
	if (ret) {
		netdev_err(tc6->netdev,
			   "Direct PHY register access is not supported by the MAC-PHY\n");
		return ret;
	}

	ret = oa_tc6_mdiobus_register(tc6);
	if (ret)
		return ret;

	tc6->phydev = phy_find_first(tc6->mdiobus);
	if (!tc6->phydev) {
		netdev_err(tc6->netdev, "No PHY found\n");
		oa_tc6_mdiobus_unregister(tc6);
		return -ENODEV;
	}

	tc6->phydev->is_internal = true;
	ret = phy_connect_direct(tc6->netdev, tc6->phydev,
				 &oa_tc6_handle_link_change,
				 PHY_INTERFACE_MODE_INTERNAL);
	if (ret) {
		netdev_err(tc6->netdev, "Can't attach PHY to %s\n",
			   tc6->mdiobus->id);
		oa_tc6_mdiobus_unregister(tc6);
		return ret;
	}

	phy_attached_info(tc6->netdev->phydev);

	return 0;
}

static void oa_tc6_phy_exit(struct oa_tc6 *tc6)
{
	phy_disconnect(tc6->phydev);
	oa_tc6_mdiobus_unregister(tc6);
}

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

static int oa_tc6_enable_data_transfer(struct oa_tc6 *tc6)
{
	u32 value;
	int ret;

	ret = oa_tc6_read_register(tc6, OA_TC6_REG_CONFIG0, &value);
	if (ret)
		return ret;

	/* Enable configuration synchronization for data transfer */
	value |= CONFIG0_SYNC;

	return oa_tc6_write_register(tc6, OA_TC6_REG_CONFIG0, value);
}

static void oa_tc6_cleanup_ongoing_rx_skb(struct oa_tc6 *tc6)
{
	if (tc6->rx_skb) {
		tc6->netdev->stats.rx_dropped++;
		kfree_skb(tc6->rx_skb);
		tc6->rx_skb = NULL;
	}
}

static void oa_tc6_cleanup_ongoing_tx_skb(struct oa_tc6 *tc6)
{
	if (tc6->ongoing_tx_skb) {
		tc6->netdev->stats.tx_dropped++;
		kfree_skb(tc6->ongoing_tx_skb);
		tc6->ongoing_tx_skb = NULL;
	}
}

static int oa_tc6_process_extended_status(struct oa_tc6 *tc6)
{
	u32 value;
	int ret;

	ret = oa_tc6_read_register(tc6, OA_TC6_REG_STATUS0, &value);
	if (ret) {
		netdev_err(tc6->netdev, "STATUS0 register read failed: %d\n",
			   ret);
		return ret;
	}

	/* Clear the error interrupts status */
	ret = oa_tc6_write_register(tc6, OA_TC6_REG_STATUS0, value);
	if (ret) {
		netdev_err(tc6->netdev, "STATUS0 register write failed: %d\n",
			   ret);
		return ret;
	}

	if (FIELD_GET(STATUS0_RX_BUFFER_OVERFLOW_ERROR, value)) {
		tc6->rx_buf_overflow = true;
		oa_tc6_cleanup_ongoing_rx_skb(tc6);
		net_err_ratelimited("%s: Receive buffer overflow error\n",
				    tc6->netdev->name);
		return -EAGAIN;
	}
	if (FIELD_GET(STATUS0_TX_PROTOCOL_ERROR, value)) {
		netdev_err(tc6->netdev, "Transmit protocol error\n");
		return -ENODEV;
	}
	/* TODO: Currently loss of frame and header errors are treated as
	 * non-recoverable errors. They will be handled in the next version.
	 */
	if (FIELD_GET(STATUS0_LOSS_OF_FRAME_ERROR, value)) {
		netdev_err(tc6->netdev, "Loss of frame error\n");
		return -ENODEV;
	}
	if (FIELD_GET(STATUS0_HEADER_ERROR, value)) {
		netdev_err(tc6->netdev, "Header error\n");
		return -ENODEV;
	}

	return 0;
}

static int oa_tc6_process_rx_chunk_footer(struct oa_tc6 *tc6, u32 footer)
{
	/* Process rx chunk footer for the following,
	 * 1. tx credits
	 * 2. errors if any from MAC-PHY
	 * 3. receive chunks available
	 */
	tc6->tx_credits = FIELD_GET(OA_TC6_DATA_FOOTER_TX_CREDITS, footer);
	tc6->rx_chunks_available = FIELD_GET(OA_TC6_DATA_FOOTER_RX_CHUNKS,
					     footer);

	if (FIELD_GET(OA_TC6_DATA_FOOTER_EXTENDED_STS, footer)) {
		int ret = oa_tc6_process_extended_status(tc6);

		if (ret)
			return ret;
	}

	/* TODO: Currently received header bad and configuration unsync errors
	 * are treated as non-recoverable errors. They will be handled in the
	 * next version.
	 */
	if (FIELD_GET(OA_TC6_DATA_FOOTER_RXD_HEADER_BAD, footer)) {
		netdev_err(tc6->netdev, "Rxd header bad error\n");
		return -ENODEV;
	}

	if (!FIELD_GET(OA_TC6_DATA_FOOTER_CONFIG_SYNC, footer)) {
		netdev_err(tc6->netdev, "Config unsync error\n");
		return -ENODEV;
	}

	return 0;
}

static void oa_tc6_submit_rx_skb(struct oa_tc6 *tc6)
{
	tc6->rx_skb->protocol = eth_type_trans(tc6->rx_skb, tc6->netdev);
	tc6->netdev->stats.rx_packets++;
	tc6->netdev->stats.rx_bytes += tc6->rx_skb->len;

	netif_rx(tc6->rx_skb);

	tc6->rx_skb = NULL;
}

static void oa_tc6_update_rx_skb(struct oa_tc6 *tc6, u8 *payload, u8 length)
{
	memcpy(skb_put(tc6->rx_skb, length), payload, length);
}

static int oa_tc6_allocate_rx_skb(struct oa_tc6 *tc6)
{
	tc6->rx_skb = netdev_alloc_skb_ip_align(tc6->netdev, tc6->netdev->mtu +
						ETH_HLEN + ETH_FCS_LEN);
	if (!tc6->rx_skb) {
		tc6->netdev->stats.rx_dropped++;
		return -ENOMEM;
	}

	return 0;
}

static int oa_tc6_prcs_complete_rx_frame(struct oa_tc6 *tc6, u8 *payload,
					 u16 size)
{
	int ret;

	ret = oa_tc6_allocate_rx_skb(tc6);
	if (ret)
		return ret;

	oa_tc6_update_rx_skb(tc6, payload, size);

	oa_tc6_submit_rx_skb(tc6);

	return 0;
}

static int oa_tc6_prcs_rx_frame_start(struct oa_tc6 *tc6, u8 *payload, u16 size)
{
	int ret;

	ret = oa_tc6_allocate_rx_skb(tc6);
	if (ret)
		return ret;

	oa_tc6_update_rx_skb(tc6, payload, size);

	return 0;
}

static void oa_tc6_prcs_rx_frame_end(struct oa_tc6 *tc6, u8 *payload, u16 size)
{
	oa_tc6_update_rx_skb(tc6, payload, size);

	oa_tc6_submit_rx_skb(tc6);
}

static void oa_tc6_prcs_ongoing_rx_frame(struct oa_tc6 *tc6, u8 *payload,
					 u32 footer)
{
	oa_tc6_update_rx_skb(tc6, payload, OA_TC6_CHUNK_PAYLOAD_SIZE);
}

static int oa_tc6_prcs_rx_chunk_payload(struct oa_tc6 *tc6, u8 *data,
					u32 footer)
{
	u8 start_byte_offset = FIELD_GET(OA_TC6_DATA_FOOTER_START_WORD_OFFSET,
					 footer) * sizeof(u32);
	u8 end_byte_offset = FIELD_GET(OA_TC6_DATA_FOOTER_END_BYTE_OFFSET,
				       footer);
	bool start_valid = FIELD_GET(OA_TC6_DATA_FOOTER_START_VALID, footer);
	bool end_valid = FIELD_GET(OA_TC6_DATA_FOOTER_END_VALID, footer);
	u16 size;

	/* Restart the new rx frame after receiving rx buffer overflow error */
	if (start_valid && tc6->rx_buf_overflow)
		tc6->rx_buf_overflow = false;

	if (tc6->rx_buf_overflow)
		return 0;

	/* Process the chunk with complete rx frame */
	if (start_valid && end_valid && start_byte_offset < end_byte_offset) {
		size = end_byte_offset + 1 - start_byte_offset;
		return oa_tc6_prcs_complete_rx_frame(tc6,
						     &data[start_byte_offset],
						     size);
	}

	/* Process the chunk with only rx frame start */
	if (start_valid && !end_valid) {
		size = OA_TC6_CHUNK_PAYLOAD_SIZE - start_byte_offset;
		return oa_tc6_prcs_rx_frame_start(tc6,
						  &data[start_byte_offset],
						  size);
	}

	/* Process the chunk with only rx frame end */
	if (end_valid && !start_valid) {
		size = end_byte_offset + 1;
		oa_tc6_prcs_rx_frame_end(tc6, data, size);
		return 0;
	}

	/* Process the chunk with previous rx frame end and next rx frame
	 * start.
	 */
	if (start_valid && end_valid && start_byte_offset > end_byte_offset) {
		/* After rx buffer overflow error received, there might be a
		 * possibility of getting an end valid of a previously
		 * incomplete rx frame along with the new rx frame start valid.
		 */
		if (tc6->rx_skb) {
			size = end_byte_offset + 1;
			oa_tc6_prcs_rx_frame_end(tc6, data, size);
		}
		size = OA_TC6_CHUNK_PAYLOAD_SIZE - start_byte_offset;
		return oa_tc6_prcs_rx_frame_start(tc6,
						  &data[start_byte_offset],
						  size);
	}

	/* Process the chunk with ongoing rx frame data */
	oa_tc6_prcs_ongoing_rx_frame(tc6, data, footer);

	return 0;
}

static u32 oa_tc6_get_rx_chunk_footer(struct oa_tc6 *tc6, u16 footer_offset)
{
	u8 *rx_buf = tc6->spi_data_rx_buf;
	__be32 footer;

	footer = *((__be32 *)&rx_buf[footer_offset]);

	return be32_to_cpu(footer);
}

static int oa_tc6_process_spi_data_rx_buf(struct oa_tc6 *tc6, u16 length)
{
	u16 no_of_rx_chunks = length / OA_TC6_CHUNK_SIZE;
	u32 footer;
	int ret;

	/* All the rx chunks in the receive SPI data buffer are examined here */
	for (int i = 0; i < no_of_rx_chunks; i++) {
		/* Last 4 bytes in each received chunk consist footer info */
		footer = oa_tc6_get_rx_chunk_footer(tc6, i * OA_TC6_CHUNK_SIZE +
						    OA_TC6_CHUNK_PAYLOAD_SIZE);

		ret = oa_tc6_process_rx_chunk_footer(tc6, footer);
		if (ret)
			return ret;

		/* If there is a data valid chunks then process it for the
		 * information needed to determine the validity and the location
		 * of the receive frame data.
		 */
		if (FIELD_GET(OA_TC6_DATA_FOOTER_DATA_VALID, footer)) {
			u8 *payload = tc6->spi_data_rx_buf + i *
				      OA_TC6_CHUNK_SIZE;

			ret = oa_tc6_prcs_rx_chunk_payload(tc6, payload,
							   footer);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static __be32 oa_tc6_prepare_data_header(bool data_valid, bool start_valid,
					 bool end_valid, u8 end_byte_offset)
{
	u32 header = FIELD_PREP(OA_TC6_DATA_HEADER_DATA_NOT_CTRL,
				OA_TC6_DATA_HEADER) |
		     FIELD_PREP(OA_TC6_DATA_HEADER_DATA_VALID, data_valid) |
		     FIELD_PREP(OA_TC6_DATA_HEADER_START_VALID, start_valid) |
		     FIELD_PREP(OA_TC6_DATA_HEADER_END_VALID, end_valid) |
		     FIELD_PREP(OA_TC6_DATA_HEADER_END_BYTE_OFFSET,
				end_byte_offset);

	header |= FIELD_PREP(OA_TC6_DATA_HEADER_PARITY,
			     oa_tc6_get_parity(header));

	return cpu_to_be32(header);
}

static void oa_tc6_add_tx_skb_to_spi_buf(struct oa_tc6 *tc6)
{
	enum oa_tc6_data_end_valid_info end_valid = OA_TC6_DATA_END_INVALID;
	__be32 *tx_buf = tc6->spi_data_tx_buf + tc6->spi_data_tx_buf_offset;
	u16 remaining_len = tc6->ongoing_tx_skb->len - tc6->tx_skb_offset;
	u8 *tx_skb_data = tc6->ongoing_tx_skb->data + tc6->tx_skb_offset;
	enum oa_tc6_data_start_valid_info start_valid;
	u8 end_byte_offset = 0;
	u16 length_to_copy;

	/* Initial value is assigned here to avoid more than 80 characters in
	 * the declaration place.
	 */
	start_valid = OA_TC6_DATA_START_INVALID;

	/* Set start valid if the current tx chunk contains the start of the tx
	 * ethernet frame.
	 */
	if (!tc6->tx_skb_offset)
		start_valid = OA_TC6_DATA_START_VALID;

	/* If the remaining tx skb length is more than the chunk payload size of
	 * 64 bytes then copy only 64 bytes and leave the ongoing tx skb for
	 * next tx chunk.
	 */
	length_to_copy = min_t(u16, remaining_len, OA_TC6_CHUNK_PAYLOAD_SIZE);

	/* Copy the tx skb data to the tx chunk payload buffer */
	memcpy(tx_buf + 1, tx_skb_data, length_to_copy);
	tc6->tx_skb_offset += length_to_copy;

	/* Set end valid if the current tx chunk contains the end of the tx
	 * ethernet frame.
	 */
	if (tc6->ongoing_tx_skb->len == tc6->tx_skb_offset) {
		end_valid = OA_TC6_DATA_END_VALID;
		end_byte_offset = length_to_copy - 1;
		tc6->tx_skb_offset = 0;
		tc6->netdev->stats.tx_bytes += tc6->ongoing_tx_skb->len;
		tc6->netdev->stats.tx_packets++;
		kfree_skb(tc6->ongoing_tx_skb);
		tc6->ongoing_tx_skb = NULL;
	}

	*tx_buf = oa_tc6_prepare_data_header(OA_TC6_DATA_VALID, start_valid,
					     end_valid, end_byte_offset);
	tc6->spi_data_tx_buf_offset += OA_TC6_CHUNK_SIZE;
}

static u16 oa_tc6_prepare_spi_tx_buf_for_tx_skbs(struct oa_tc6 *tc6)
{
	u16 used_tx_credits;

	/* Get tx skbs and convert them into tx chunks based on the tx credits
	 * available.
	 */
	for (used_tx_credits = 0; used_tx_credits < tc6->tx_credits;
	     used_tx_credits++) {
		if (!tc6->ongoing_tx_skb) {
			spin_lock_bh(&tc6->tx_skb_lock);
			tc6->ongoing_tx_skb = tc6->waiting_tx_skb;
			tc6->waiting_tx_skb = NULL;
			spin_unlock_bh(&tc6->tx_skb_lock);
		}
		if (!tc6->ongoing_tx_skb)
			break;
		oa_tc6_add_tx_skb_to_spi_buf(tc6);
	}

	return used_tx_credits * OA_TC6_CHUNK_SIZE;
}

static void oa_tc6_add_empty_chunks_to_spi_buf(struct oa_tc6 *tc6,
					       u16 needed_empty_chunks)
{
	__be32 header;

	header = oa_tc6_prepare_data_header(OA_TC6_DATA_INVALID,
					    OA_TC6_DATA_START_INVALID,
					    OA_TC6_DATA_END_INVALID, 0);

	while (needed_empty_chunks--) {
		__be32 *tx_buf = tc6->spi_data_tx_buf +
				 tc6->spi_data_tx_buf_offset;

		*tx_buf = header;
		tc6->spi_data_tx_buf_offset += OA_TC6_CHUNK_SIZE;
	}
}

static u16 oa_tc6_prepare_spi_tx_buf_for_rx_chunks(struct oa_tc6 *tc6, u16 len)
{
	u16 tx_chunks = len / OA_TC6_CHUNK_SIZE;
	u16 needed_empty_chunks;

	/* If there are more chunks to receive than to transmit, we need to add
	 * enough empty tx chunks to allow the reception of the excess rx
	 * chunks.
	 */
	if (tx_chunks >= tc6->rx_chunks_available)
		return len;

	needed_empty_chunks = tc6->rx_chunks_available - tx_chunks;

	oa_tc6_add_empty_chunks_to_spi_buf(tc6, needed_empty_chunks);

	return needed_empty_chunks * OA_TC6_CHUNK_SIZE + len;
}

static int oa_tc6_try_spi_transfer(struct oa_tc6 *tc6)
{
	int ret;

	while (true) {
		u16 spi_len = 0;

		tc6->spi_data_tx_buf_offset = 0;

		if (tc6->ongoing_tx_skb || tc6->waiting_tx_skb)
			spi_len = oa_tc6_prepare_spi_tx_buf_for_tx_skbs(tc6);

		spi_len = oa_tc6_prepare_spi_tx_buf_for_rx_chunks(tc6, spi_len);

		if (tc6->int_flag) {
			tc6->int_flag = false;
			if (spi_len == 0) {
				oa_tc6_add_empty_chunks_to_spi_buf(tc6, 1);
				spi_len = OA_TC6_CHUNK_SIZE;
			}
		}

		if (spi_len == 0)
			break;

		ret = oa_tc6_spi_transfer(tc6, OA_TC6_DATA_HEADER, spi_len);
		if (ret) {
			netdev_err(tc6->netdev, "SPI data transfer failed: %d\n",
				   ret);
			return ret;
		}

		ret = oa_tc6_process_spi_data_rx_buf(tc6, spi_len);
		if (ret) {
			if (ret == -EAGAIN)
				continue;

			oa_tc6_cleanup_ongoing_tx_skb(tc6);
			oa_tc6_cleanup_ongoing_rx_skb(tc6);
			netdev_err(tc6->netdev, "Device error: %d\n", ret);
			return ret;
		}

		if (!tc6->waiting_tx_skb && netif_queue_stopped(tc6->netdev))
			netif_wake_queue(tc6->netdev);
	}

	return 0;
}

static int oa_tc6_spi_thread_handler(void *data)
{
	struct oa_tc6 *tc6 = data;
	int ret;

	while (likely(!kthread_should_stop())) {
		/* This kthread will be waken up if there is a tx skb or mac-phy
		 * interrupt to perform spi transfer with tx chunks.
		 */
		wait_event_interruptible(tc6->spi_wq, tc6->int_flag ||
					 (tc6->waiting_tx_skb &&
					 tc6->tx_credits) ||
					 kthread_should_stop());

		if (kthread_should_stop())
			break;

		ret = oa_tc6_try_spi_transfer(tc6);
		if (ret)
			return ret;
	}

	return 0;
}

static int oa_tc6_update_buffer_status_from_register(struct oa_tc6 *tc6)
{
	u32 value;
	int ret;

	/* Initially tx credits and rx chunks available to be updated from the
	 * register as there is no data transfer performed yet. Later they will
	 * be updated from the rx footer.
	 */
	ret = oa_tc6_read_register(tc6, OA_TC6_REG_BUFFER_STATUS, &value);
	if (ret)
		return ret;

	tc6->tx_credits = FIELD_GET(BUFFER_STATUS_TX_CREDITS_AVAILABLE, value);
	tc6->rx_chunks_available = FIELD_GET(BUFFER_STATUS_RX_CHUNKS_AVAILABLE,
					     value);

	return 0;
}

static irqreturn_t oa_tc6_macphy_isr(int irq, void *data)
{
	struct oa_tc6 *tc6 = data;

	/* MAC-PHY interrupt can occur for the following reasons.
	 * - availability of tx credits if it was 0 before and not reported in
	 *   the previous rx footer.
	 * - availability of rx chunks if it was 0 before and not reported in
	 *   the previous rx footer.
	 * - extended status event not reported in the previous rx footer.
	 */
	tc6->int_flag = true;
	/* Wake spi kthread to perform spi transfer */
	wake_up_interruptible(&tc6->spi_wq);

	return IRQ_HANDLED;
}

/**
 * oa_tc6_zero_align_receive_frame_enable - function to enable zero align
 * receive frame feature.
 * @tc6: oa_tc6 struct.
 *
 * Return: 0 on success otherwise failed.
 */
int oa_tc6_zero_align_receive_frame_enable(struct oa_tc6 *tc6)
{
	u32 regval;
	int ret;

	ret = oa_tc6_read_register(tc6, OA_TC6_REG_CONFIG0, &regval);
	if (ret)
		return ret;

	/* Set Zero-Align Receive Frame Enable */
	regval |= CONFIG0_ZARFE_ENABLE;

	return oa_tc6_write_register(tc6, OA_TC6_REG_CONFIG0, regval);
}
EXPORT_SYMBOL_GPL(oa_tc6_zero_align_receive_frame_enable);

/**
 * oa_tc6_start_xmit - function for sending the tx skb which consists ethernet
 * frame.
 * @tc6: oa_tc6 struct.
 * @skb: socket buffer in which the ethernet frame is stored.
 *
 * Return: NETDEV_TX_OK if the transmit ethernet frame skb added in the tx_skb_q
 * otherwise returns NETDEV_TX_BUSY.
 */
netdev_tx_t oa_tc6_start_xmit(struct oa_tc6 *tc6, struct sk_buff *skb)
{
	if (tc6->waiting_tx_skb) {
		netif_stop_queue(tc6->netdev);
		return NETDEV_TX_BUSY;
	}

	if (skb_linearize(skb)) {
		dev_kfree_skb_any(skb);
		tc6->netdev->stats.tx_dropped++;
		return NETDEV_TX_OK;
	}

	spin_lock_bh(&tc6->tx_skb_lock);
	tc6->waiting_tx_skb = skb;
	spin_unlock_bh(&tc6->tx_skb_lock);

	/* Wake spi kthread to perform spi transfer */
	wake_up_interruptible(&tc6->spi_wq);

	return NETDEV_TX_OK;
}
EXPORT_SYMBOL_GPL(oa_tc6_start_xmit);

/**
 * oa_tc6_init - allocates and initializes oa_tc6 structure.
 * @spi: device with which data will be exchanged.
 * @netdev: network device interface structure.
 *
 * Return: pointer reference to the oa_tc6 structure if the MAC-PHY
 * initialization is successful otherwise NULL.
 */
struct oa_tc6 *oa_tc6_init(struct spi_device *spi, struct net_device *netdev)
{
	struct oa_tc6 *tc6;
	int ret;

	tc6 = devm_kzalloc(&spi->dev, sizeof(*tc6), GFP_KERNEL);
	if (!tc6)
		return NULL;

	tc6->spi = spi;
	tc6->netdev = netdev;
	SET_NETDEV_DEV(netdev, &spi->dev);
	mutex_init(&tc6->spi_ctrl_lock);
	spin_lock_init(&tc6->tx_skb_lock);

	/* Set the SPI controller to pump at realtime priority */
	tc6->spi->rt = true;
	if (spi_setup(tc6->spi) < 0)
		return NULL;

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

	tc6->spi_data_tx_buf = devm_kzalloc(&tc6->spi->dev,
					    OA_TC6_SPI_DATA_BUF_SIZE,
					    GFP_KERNEL);
	if (!tc6->spi_data_tx_buf)
		return NULL;

	tc6->spi_data_rx_buf = devm_kzalloc(&tc6->spi->dev,
					    OA_TC6_SPI_DATA_BUF_SIZE,
					    GFP_KERNEL);
	if (!tc6->spi_data_rx_buf)
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

	ret = oa_tc6_phy_init(tc6);
	if (ret) {
		dev_err(&tc6->spi->dev,
			"MAC internal PHY initialization failed: %d\n", ret);
		return NULL;
	}

	ret = oa_tc6_enable_data_transfer(tc6);
	if (ret) {
		dev_err(&tc6->spi->dev, "Failed to enable data transfer: %d\n",
			ret);
		goto phy_exit;
	}

	ret = oa_tc6_update_buffer_status_from_register(tc6);
	if (ret) {
		dev_err(&tc6->spi->dev,
			"Failed to update buffer status: %d\n", ret);
		goto phy_exit;
	}

	init_waitqueue_head(&tc6->spi_wq);

	tc6->spi_thread = kthread_run(oa_tc6_spi_thread_handler, tc6,
				      "oa-tc6-spi-thread");
	if (IS_ERR(tc6->spi_thread)) {
		dev_err(&tc6->spi->dev, "Failed to create SPI thread\n");
		goto phy_exit;
	}

	sched_set_fifo(tc6->spi_thread);

	ret = devm_request_irq(&tc6->spi->dev, tc6->spi->irq, oa_tc6_macphy_isr,
			       IRQF_TRIGGER_FALLING, dev_name(&tc6->spi->dev),
			       tc6);
	if (ret) {
		dev_err(&tc6->spi->dev, "Failed to request macphy isr %d\n",
			ret);
		goto kthread_stop;
	}

	/* oa_tc6_sw_reset_macphy() function resets and clears the MAC-PHY reset
	 * complete status. IRQ is also asserted on reset completion and it is
	 * remain asserted until MAC-PHY receives a data chunk. So performing an
	 * empty data chunk transmission will deassert the IRQ. Refer section
	 * 7.7 and 9.2.8.8 in the OPEN Alliance specification for more details.
	 */
	tc6->int_flag = true;
	wake_up_interruptible(&tc6->spi_wq);

	return tc6;

kthread_stop:
	kthread_stop(tc6->spi_thread);
phy_exit:
	oa_tc6_phy_exit(tc6);
	return NULL;
}
EXPORT_SYMBOL_GPL(oa_tc6_init);

/**
 * oa_tc6_exit - exit function.
 * @tc6: oa_tc6 struct.
 */
void oa_tc6_exit(struct oa_tc6 *tc6)
{
	oa_tc6_phy_exit(tc6);
	kthread_stop(tc6->spi_thread);
	dev_kfree_skb_any(tc6->ongoing_tx_skb);
	dev_kfree_skb_any(tc6->waiting_tx_skb);
	dev_kfree_skb_any(tc6->rx_skb);
}
EXPORT_SYMBOL_GPL(oa_tc6_exit);

MODULE_DESCRIPTION("OPEN Alliance 10BASE‑T1x MAC‑PHY Serial Interface Lib");
MODULE_AUTHOR("Parthiban Veerasooran <parthiban.veerasooran@microchip.com>");
MODULE_LICENSE("GPL");
