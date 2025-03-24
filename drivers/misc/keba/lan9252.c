// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) KEBA Industrial Automation Gmbh 2024
 *
 * Driver for LAN9252 on KEBA CP500 devices
 *
 * This driver is used for updating the configuration of the LAN9252 controller
 * on KEBA CP500 devices. The LAN9252 is connected over SPI, which is also named
 * PDI.
 */

#include <linux/spi/spi.h>
#include <linux/mii.h>

/* SPI commands */
#define LAN9252_SPI_READ	0x3
#define LAN9252_SPI_WRITE	0x2

struct lan9252_read_cmd {
	u8 cmd;
	u8 addr_0;
	u8 addr_1;
} __packed;

struct lan9252_write_cmd {
	u8 cmd;
	u8 addr_0;
	u8 addr_1;
	u32 data;
} __packed;

/* byte test register */
#define LAN9252_BYTE_TEST		0x64
#define   LAN9252_BYTE_TEST_VALUE	0x87654321

/* hardware configuration register */
#define LAN9252_HW_CFG		0x74
#define   LAN9252_HW_CFG_READY	0x08000000

/* EtherCAT CSR interface data register */
#define LAN9252_ECAT_CSR_DATA	0x300

/* EtherCAT CSR interface command register */
#define LAN9252_ECAT_CSR_CMD	0x304
#define   LAN9252_ECAT_CSR_BUSY	0x80000000
#define   LAN9252_ECAT_CSR_READ	0x40000000

/* EtherCAT slave controller MII register */
#define LAN9252_ESC_MII			0x510
#define   LAN9252_ESC_MII_BUSY		0x8000
#define   LAN9252_ESC_MII_CMD_ERR	0x4000
#define   LAN9252_ESC_MII_READ_ERR	0x2000
#define   LAN9252_ESC_MII_ERR_MASK	(LAN9252_ESC_MII_CMD_ERR | \
					 LAN9252_ESC_MII_READ_ERR)
#define   LAN9252_ESC_MII_WRITE		0x0200
#define   LAN9252_ESC_MII_READ		0x0100

/* EtherCAT slave controller PHY address register */
#define LAN9252_ESC_PHY_ADDR		0x512

/* EtherCAT slave controller PHY register address register */
#define LAN9252_ESC_PHY_REG_ADDR	0x513

/* EtherCAT slave controller PHY data register */
#define LAN9252_ESC_PHY_DATA		0x514

/* EtherCAT slave controller PDI access state register */
#define LAN9252_ESC_MII_PDI		0x517
#define   LAN9252_ESC_MII_ACCESS_PDI	0x01
#define   LAN9252_ESC_MII_ACCESS_ECAT	0x00

/* PHY address */
#define PHY_ADDRESS	2

#define SPI_RETRY_COUNT		10
#define SPI_WAIT_US		100
#define SPI_CSR_WAIT_US		500

static int lan9252_spi_read(struct spi_device *spi, u16 addr, u32 *data)
{
	struct lan9252_read_cmd cmd;

	cmd.cmd = LAN9252_SPI_READ;
	cmd.addr_0 = (addr >> 8) & 0xFF;
	cmd.addr_1 = addr & 0xFF;

	return spi_write_then_read(spi, (u8 *)&cmd,
				   sizeof(struct lan9252_read_cmd),
				   (u8 *)data, sizeof(u32));
}

static int lan9252_spi_write(struct spi_device *spi, u16 addr, u32 data)
{
	struct lan9252_write_cmd cmd;

	cmd.cmd = LAN9252_SPI_WRITE;
	cmd.addr_0 = (addr >> 8) & 0xFF;
	cmd.addr_1 = addr & 0xFF;
	cmd.data = data;

	return spi_write(spi, (u8 *)&cmd, sizeof(struct lan9252_write_cmd));
}

static bool lan9252_init(struct spi_device *spi)
{
	u32 data;
	int ret;

	ret = lan9252_spi_read(spi, LAN9252_BYTE_TEST, &data);
	if (ret || data != LAN9252_BYTE_TEST_VALUE)
		return false;

	ret = lan9252_spi_read(spi, LAN9252_HW_CFG, &data);
	if (ret || !(data & LAN9252_HW_CFG_READY))
		return false;

	return true;
}

static u8 lan9252_esc_get_size(u16 addr)
{
	if (addr == LAN9252_ESC_MII || addr == LAN9252_ESC_PHY_DATA)
		return 2;

	return 1;
}

static int lan9252_esc_wait(struct spi_device *spi)
{
	ktime_t timeout = ktime_add_us(ktime_get(), SPI_WAIT_US);
	u32 data;
	int ret;

	/* wait while CSR command is busy */
	for (;;) {
		ret = lan9252_spi_read(spi, LAN9252_ECAT_CSR_CMD, &data);
		if (ret)
			return ret;
		if (!(data & LAN9252_ECAT_CSR_BUSY))
			return 0;

		if (ktime_compare(ktime_get(), timeout) > 0) {
			ret = lan9252_spi_read(spi, LAN9252_ECAT_CSR_CMD, &data);
			if (ret)
				return ret;
			break;
		}
	}

	return (!(data & LAN9252_ECAT_CSR_BUSY)) ? 0 : -ETIMEDOUT;
}

static int lan9252_esc_read(struct spi_device *spi, u16 addr, u32 *data)
{
	u32 csr_cmd;
	u8 size;
	int ret;

	size = lan9252_esc_get_size(addr);
	csr_cmd = LAN9252_ECAT_CSR_BUSY | LAN9252_ECAT_CSR_READ;
	csr_cmd |= (size << 16) | addr;
	ret = lan9252_spi_write(spi, LAN9252_ECAT_CSR_CMD, csr_cmd);
	if (ret)
		return ret;

	ret = lan9252_esc_wait(spi);
	if (ret)
		return ret;

	ret = lan9252_spi_read(spi, LAN9252_ECAT_CSR_DATA, data);
	if (ret)
		return ret;

	return 0;
}

static int lan9252_esc_write(struct spi_device *spi, u16 addr, u32 data)
{
	u32 csr_cmd;
	u8 size;
	int ret;

	ret = lan9252_spi_write(spi, LAN9252_ECAT_CSR_DATA, data);
	if (ret)
		return ret;

	size = lan9252_esc_get_size(addr);
	csr_cmd = LAN9252_ECAT_CSR_BUSY;
	csr_cmd |= (size << 16) | addr;
	ret = lan9252_spi_write(spi, LAN9252_ECAT_CSR_CMD, csr_cmd);
	if (ret)
		return ret;

	ret = lan9252_esc_wait(spi);
	if (ret)
		return ret;

	return 0;
}

static int lan9252_access_mii(struct spi_device *spi, bool access)
{
	u32 data;

	if (access)
		data = LAN9252_ESC_MII_ACCESS_PDI;
	else
		data = LAN9252_ESC_MII_ACCESS_ECAT;

	return lan9252_esc_write(spi, LAN9252_ESC_MII_PDI, data);
}

static int lan9252_mii_wait(struct spi_device *spi)
{
	ktime_t timeout = ktime_add_us(ktime_get(), SPI_CSR_WAIT_US);
	u32 data;
	int ret;

	/* wait while MII control state machine is busy */
	for (;;) {
		ret = lan9252_esc_read(spi, LAN9252_ESC_MII, &data);
		if (ret)
			return ret;
		if (data & LAN9252_ESC_MII_ERR_MASK)
			return -EIO;
		if (!(data & LAN9252_ESC_MII_BUSY))
			return 0;

		if (ktime_compare(ktime_get(), timeout) > 0) {
			ret = lan9252_esc_read(spi, LAN9252_ESC_MII, &data);
			if (ret)
				return ret;
			if (data & LAN9252_ESC_MII_ERR_MASK)
				return -EIO;
			break;
		}
	}

	return (!(data & LAN9252_ESC_MII_BUSY)) ? 0 : -ETIMEDOUT;
}

static int lan9252_mii_read(struct spi_device *spi, u8 phy_addr, u8 reg_addr,
			    u32 *data)
{
	int ret;

	ret = lan9252_esc_write(spi, LAN9252_ESC_PHY_ADDR, phy_addr);
	if (ret)
		return ret;
	ret = lan9252_esc_write(spi, LAN9252_ESC_PHY_REG_ADDR, reg_addr);
	if (ret)
		return ret;

	ret = lan9252_esc_write(spi, LAN9252_ESC_MII, LAN9252_ESC_MII_READ);
	if (ret)
		return ret;

	ret = lan9252_mii_wait(spi);
	if (ret)
		return ret;

	return lan9252_esc_read(spi, LAN9252_ESC_PHY_DATA, data);
}

static int lan9252_mii_write(struct spi_device *spi, u8 phy_addr, u8 reg_addr,
			     u32 data)
{
	int ret;

	ret = lan9252_esc_write(spi, LAN9252_ESC_PHY_ADDR, phy_addr);
	if (ret)
		return ret;
	ret = lan9252_esc_write(spi, LAN9252_ESC_PHY_REG_ADDR, reg_addr);
	if (ret)
		return ret;
	ret = lan9252_esc_write(spi, LAN9252_ESC_PHY_DATA, data);
	if (ret)
		return ret;

	ret = lan9252_esc_write(spi, LAN9252_ESC_MII, LAN9252_ESC_MII_WRITE);
	if (ret)
		return ret;

	return lan9252_mii_wait(spi);
}

static int lan9252_probe(struct spi_device *spi)
{
	u32 data;
	int retry = SPI_RETRY_COUNT;
	int ret;

	/* execute specified initialization sequence */
	while (retry && !lan9252_init(spi))
		retry--;
	if (retry == 0) {
		dev_err(&spi->dev,
			"Can't initialize LAN9252 SPI communication!");
		return -EIO;
	}

	/* enable access to MII management for PDI */
	ret = lan9252_access_mii(spi, true);
	if (ret) {
		dev_err(&spi->dev, "Can't enable access to MII management!");
		return ret;
	}

	/*
	 * check PHY configuration and configure if necessary
	 *	- full duplex
	 *	- auto negotiation disabled
	 *	- 100 Mbps
	 */
	ret = lan9252_mii_read(spi, PHY_ADDRESS, MII_BMCR, &data);
	if (ret) {
		dev_err(&spi->dev, "Can't read LAN9252 configuration!");
		goto out;
	}
	if (!(data & BMCR_FULLDPLX) || (data & BMCR_ANENABLE) ||
	    !(data & BMCR_SPEED100)) {
		/*
		 */
		data &= ~(BMCR_ANENABLE);
		data |= (BMCR_FULLDPLX | BMCR_SPEED100);
		ret = lan9252_mii_write(spi, PHY_ADDRESS, MII_BMCR, data);
		if (ret)
			dev_err(&spi->dev,
				"Can't write LAN9252 configuration!");
	}

	dev_info(&spi->dev, "LAN9252 PHY configuration");

out:
	/* disable access to MII management for PDI */
	lan9252_access_mii(spi, false);

	return ret;
}

static const struct spi_device_id lan9252_id[] = {
	{"lan9252"},
	{}
};
MODULE_DEVICE_TABLE(spi, lan9252_id);

static struct spi_driver lan9252_driver = {
	.driver = {
		.name	= "lan9252",
	},
	.probe		= lan9252_probe,
	.id_table	= lan9252_id,
};
module_spi_driver(lan9252_driver);

MODULE_AUTHOR("Petar Bojanic <boja@keba.com>");
MODULE_AUTHOR("Gerhard Engleder <eg@keba.com>");
MODULE_DESCRIPTION("KEBA LAN9252 driver");
MODULE_LICENSE("GPL");
