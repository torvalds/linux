// SPDX-License-Identifier: GPL-2.0
/*
 * Aeonsemi AS21XXxX PHY Driver
 *
 * Author: Christian Marangi <ansuelsmth@gmail.com>
 */

#include <linux/bitfield.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy.h>

#define VEND1_GLB_REG_CPU_RESET_ADDR_LO_BASEADDR 0x3
#define VEND1_GLB_REG_CPU_RESET_ADDR_HI_BASEADDR 0x4

#define VEND1_GLB_REG_CPU_CTRL		0xe
#define   VEND1_GLB_CPU_CTRL_MASK	GENMASK(4, 0)
#define   VEND1_GLB_CPU_CTRL_LED_POLARITY_MASK GENMASK(12, 8)
#define   VEND1_GLB_CPU_CTRL_LED_POLARITY(_n) FIELD_PREP(VEND1_GLB_CPU_CTRL_LED_POLARITY_MASK, \
							 BIT(_n))

#define VEND1_FW_START_ADDR		0x100

#define VEND1_GLB_REG_MDIO_INDIRECT_ADDRCMD 0x101
#define VEND1_GLB_REG_MDIO_INDIRECT_LOAD 0x102

#define VEND1_GLB_REG_MDIO_INDIRECT_STATUS 0x103

#define VEND1_PTP_CLK			0x142
#define   VEND1_PTP_CLK_EN		BIT(6)

/* 5 LED at step of 0x20
 * FE: Fast-Ethernet (10/100)
 * GE: Gigabit-Ethernet (1000)
 * NG: New-Generation (2500/5000/10000)
 */
#define VEND1_LED_REG(_n)		(0x1800 + ((_n) * 0x10))
#define   VEND1_LED_REG_A_EVENT		GENMASK(15, 11)
#define VEND1_LED_CONF			0x1881
#define   VEND1_LED_CONFG_BLINK		GENMASK(7, 0)

#define VEND1_SPEED_STATUS		0x4002
#define   VEND1_SPEED_MASK		GENMASK(7, 0)
#define   VEND1_SPEED_10000		FIELD_PREP_CONST(VEND1_SPEED_MASK, 0x3)
#define   VEND1_SPEED_5000		FIELD_PREP_CONST(VEND1_SPEED_MASK, 0x5)
#define   VEND1_SPEED_2500		FIELD_PREP_CONST(VEND1_SPEED_MASK, 0x9)
#define   VEND1_SPEED_1000		FIELD_PREP_CONST(VEND1_SPEED_MASK, 0x10)
#define   VEND1_SPEED_100		FIELD_PREP_CONST(VEND1_SPEED_MASK, 0x20)
#define   VEND1_SPEED_10		FIELD_PREP_CONST(VEND1_SPEED_MASK, 0x0)

#define VEND1_IPC_CMD			0x5801
#define   AEON_IPC_CMD_PARITY		BIT(15)
#define   AEON_IPC_CMD_SIZE		GENMASK(10, 6)
#define   AEON_IPC_CMD_OPCODE		GENMASK(5, 0)

#define IPC_CMD_NOOP			0x0  /* Do nothing */
#define IPC_CMD_INFO			0x1  /* Get Firmware Version */
#define IPC_CMD_SYS_CPU			0x2  /* SYS_CPU */
#define IPC_CMD_BULK_DATA		0xa  /* Pass bulk data in ipc registers. */
#define IPC_CMD_BULK_WRITE		0xc  /* Write bulk data to memory */
#define IPC_CMD_CFG_PARAM		0x1a /* Write config parameters to memory */
#define IPC_CMD_NG_TESTMODE		0x1b /* Set NG test mode and tone */
#define IPC_CMD_TEMP_MON		0x15 /* Temperature monitoring function */
#define IPC_CMD_SET_LED			0x23 /* Set led */

#define VEND1_IPC_STS			0x5802
#define   AEON_IPC_STS_PARITY		BIT(15)
#define   AEON_IPC_STS_SIZE		GENMASK(14, 10)
#define   AEON_IPC_STS_OPCODE		GENMASK(9, 4)
#define   AEON_IPC_STS_STATUS		GENMASK(3, 0)
#define   AEON_IPC_STS_STATUS_RCVD	FIELD_PREP_CONST(AEON_IPC_STS_STATUS, 0x1)
#define   AEON_IPC_STS_STATUS_PROCESS	FIELD_PREP_CONST(AEON_IPC_STS_STATUS, 0x2)
#define   AEON_IPC_STS_STATUS_SUCCESS	FIELD_PREP_CONST(AEON_IPC_STS_STATUS, 0x4)
#define   AEON_IPC_STS_STATUS_ERROR	FIELD_PREP_CONST(AEON_IPC_STS_STATUS, 0x8)
#define   AEON_IPC_STS_STATUS_BUSY	FIELD_PREP_CONST(AEON_IPC_STS_STATUS, 0xe)
#define   AEON_IPC_STS_STATUS_READY	FIELD_PREP_CONST(AEON_IPC_STS_STATUS, 0xf)

#define VEND1_IPC_DATA0			0x5808
#define VEND1_IPC_DATA1			0x5809
#define VEND1_IPC_DATA2			0x580a
#define VEND1_IPC_DATA3			0x580b
#define VEND1_IPC_DATA4			0x580c
#define VEND1_IPC_DATA5			0x580d
#define VEND1_IPC_DATA6			0x580e
#define VEND1_IPC_DATA7			0x580f
#define VEND1_IPC_DATA(_n)		(VEND1_IPC_DATA0 + (_n))

/* Sub command of CMD_INFO */
#define IPC_INFO_VERSION		0x1

/* Sub command of CMD_SYS_CPU */
#define IPC_SYS_CPU_REBOOT		0x3
#define IPC_SYS_CPU_IMAGE_OFST		0x4
#define IPC_SYS_CPU_IMAGE_CHECK		0x5
#define IPC_SYS_CPU_PHY_ENABLE		0x6

/* Sub command of CMD_CFG_PARAM */
#define IPC_CFG_PARAM_DIRECT		0x4

/* CFG DIRECT sub command */
#define IPC_CFG_PARAM_DIRECT_NG_PHYCTRL	0x1
#define IPC_CFG_PARAM_DIRECT_CU_AN	0x2
#define IPC_CFG_PARAM_DIRECT_SDS_PCS	0x3
#define IPC_CFG_PARAM_DIRECT_AUTO_EEE	0x4
#define IPC_CFG_PARAM_DIRECT_SDS_PMA	0x5
#define IPC_CFG_PARAM_DIRECT_DPC_RA	0x6
#define IPC_CFG_PARAM_DIRECT_DPC_PKT_CHK 0x7
#define IPC_CFG_PARAM_DIRECT_DPC_SDS_WAIT_ETH 0x8
#define IPC_CFG_PARAM_DIRECT_WDT	0x9
#define IPC_CFG_PARAM_DIRECT_SDS_RESTART_AN 0x10
#define IPC_CFG_PARAM_DIRECT_TEMP_MON	0x11
#define IPC_CFG_PARAM_DIRECT_WOL	0x12

/* Sub command of CMD_TEMP_MON */
#define IPC_CMD_TEMP_MON_GET		0x4

#define AS21XXX_MDIO_AN_C22		0xffe0

#define PHY_ID_AS21XXX			0x75009410
/* AS21xxx ID Legend
 * AS21x1xxB1
 *     ^ ^^
 *     | |J: Supports SyncE/PTP
 *     | |P: No SyncE/PTP support
 *     | 1: Supports 2nd Serdes
 *     | 2: Not 2nd Serdes support
 *     0: 10G, 5G, 2.5G
 *     5: 5G, 2.5G
 *     2: 2.5G
 */
#define PHY_ID_AS21011JB1		0x75009402
#define PHY_ID_AS21011PB1		0x75009412
#define PHY_ID_AS21010JB1		0x75009422
#define PHY_ID_AS21010PB1		0x75009432
#define PHY_ID_AS21511JB1		0x75009442
#define PHY_ID_AS21511PB1		0x75009452
#define PHY_ID_AS21510JB1		0x75009462
#define PHY_ID_AS21510PB1		0x75009472
#define PHY_ID_AS21210JB1		0x75009482
#define PHY_ID_AS21210PB1		0x75009492
#define PHY_VENDOR_AEONSEMI		0x75009400

#define AEON_MAX_LEDS			5
#define AEON_IPC_DELAY			10000
#define AEON_IPC_TIMEOUT		(AEON_IPC_DELAY * 100)
#define AEON_IPC_DATA_NUM_REGISTERS	8
#define AEON_IPC_DATA_MAX		(AEON_IPC_DATA_NUM_REGISTERS * sizeof(u16))

#define AEON_BOOT_ADDR			0x1000
#define AEON_CPU_BOOT_ADDR		0x2000
#define AEON_CPU_CTRL_FW_LOAD		(BIT(4) | BIT(2) | BIT(1) | BIT(0))
#define AEON_CPU_CTRL_FW_START		BIT(0)

enum as21xxx_led_event {
	VEND1_LED_REG_A_EVENT_ON_10 = 0x0,
	VEND1_LED_REG_A_EVENT_ON_100,
	VEND1_LED_REG_A_EVENT_ON_1000,
	VEND1_LED_REG_A_EVENT_ON_2500,
	VEND1_LED_REG_A_EVENT_ON_5000,
	VEND1_LED_REG_A_EVENT_ON_10000,
	VEND1_LED_REG_A_EVENT_ON_FE_GE,
	VEND1_LED_REG_A_EVENT_ON_NG,
	VEND1_LED_REG_A_EVENT_ON_FULL_DUPLEX,
	VEND1_LED_REG_A_EVENT_ON_COLLISION,
	VEND1_LED_REG_A_EVENT_BLINK_TX,
	VEND1_LED_REG_A_EVENT_BLINK_RX,
	VEND1_LED_REG_A_EVENT_BLINK_ACT,
	VEND1_LED_REG_A_EVENT_ON_LINK,
	VEND1_LED_REG_A_EVENT_ON_LINK_BLINK_ACT,
	VEND1_LED_REG_A_EVENT_ON_LINK_BLINK_RX,
	VEND1_LED_REG_A_EVENT_ON_FE_GE_BLINK_ACT,
	VEND1_LED_REG_A_EVENT_ON_NG_BLINK_ACT,
	VEND1_LED_REG_A_EVENT_ON_NG_BLINK_FE_GE,
	VEND1_LED_REG_A_EVENT_ON_FD_BLINK_COLLISION,
	VEND1_LED_REG_A_EVENT_ON,
	VEND1_LED_REG_A_EVENT_OFF,
};

struct as21xxx_led_pattern_info {
	unsigned int pattern;
	u16 val;
};

struct as21xxx_priv {
	bool parity_status;
	/* Protect concurrent IPC access */
	struct mutex ipc_lock;
};

static struct as21xxx_led_pattern_info as21xxx_led_supported_pattern[] = {
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK_10),
		.val = VEND1_LED_REG_A_EVENT_ON_10
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK_100),
		.val = VEND1_LED_REG_A_EVENT_ON_100
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK_1000),
		.val = VEND1_LED_REG_A_EVENT_ON_1000
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK_2500),
		.val = VEND1_LED_REG_A_EVENT_ON_2500
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK_5000),
		.val = VEND1_LED_REG_A_EVENT_ON_5000
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK_10000),
		.val = VEND1_LED_REG_A_EVENT_ON_10000
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK),
		.val = VEND1_LED_REG_A_EVENT_ON_LINK
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK_10) |
			   BIT(TRIGGER_NETDEV_LINK_100) |
			   BIT(TRIGGER_NETDEV_LINK_1000),
		.val = VEND1_LED_REG_A_EVENT_ON_FE_GE
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK_2500) |
			   BIT(TRIGGER_NETDEV_LINK_5000) |
			   BIT(TRIGGER_NETDEV_LINK_10000),
		.val = VEND1_LED_REG_A_EVENT_ON_NG
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_FULL_DUPLEX),
		.val = VEND1_LED_REG_A_EVENT_ON_FULL_DUPLEX
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_TX),
		.val = VEND1_LED_REG_A_EVENT_BLINK_TX
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_RX),
		.val = VEND1_LED_REG_A_EVENT_BLINK_RX
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_TX) |
			   BIT(TRIGGER_NETDEV_RX),
		.val = VEND1_LED_REG_A_EVENT_BLINK_ACT
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK_10) |
			   BIT(TRIGGER_NETDEV_LINK_100) |
			   BIT(TRIGGER_NETDEV_LINK_1000) |
			   BIT(TRIGGER_NETDEV_LINK_2500) |
			   BIT(TRIGGER_NETDEV_LINK_5000) |
			   BIT(TRIGGER_NETDEV_LINK_10000),
		.val = VEND1_LED_REG_A_EVENT_ON_LINK
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK_10) |
			   BIT(TRIGGER_NETDEV_LINK_100) |
			   BIT(TRIGGER_NETDEV_LINK_1000) |
			   BIT(TRIGGER_NETDEV_LINK_2500) |
			   BIT(TRIGGER_NETDEV_LINK_5000) |
			   BIT(TRIGGER_NETDEV_LINK_10000) |
			   BIT(TRIGGER_NETDEV_TX) |
			   BIT(TRIGGER_NETDEV_RX),
		.val = VEND1_LED_REG_A_EVENT_ON_LINK_BLINK_ACT
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK_10) |
			   BIT(TRIGGER_NETDEV_LINK_100) |
			   BIT(TRIGGER_NETDEV_LINK_1000) |
			   BIT(TRIGGER_NETDEV_LINK_2500) |
			   BIT(TRIGGER_NETDEV_LINK_5000) |
			   BIT(TRIGGER_NETDEV_LINK_10000) |
			   BIT(TRIGGER_NETDEV_RX),
		.val = VEND1_LED_REG_A_EVENT_ON_LINK_BLINK_RX
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK_10) |
			   BIT(TRIGGER_NETDEV_LINK_100) |
			   BIT(TRIGGER_NETDEV_LINK_1000) |
			   BIT(TRIGGER_NETDEV_TX) |
			   BIT(TRIGGER_NETDEV_RX),
		.val = VEND1_LED_REG_A_EVENT_ON_FE_GE_BLINK_ACT
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK_2500) |
			   BIT(TRIGGER_NETDEV_LINK_5000) |
			   BIT(TRIGGER_NETDEV_LINK_10000) |
			   BIT(TRIGGER_NETDEV_TX) |
			   BIT(TRIGGER_NETDEV_RX),
		.val = VEND1_LED_REG_A_EVENT_ON_NG_BLINK_ACT
	}
};

static int aeon_firmware_boot(struct phy_device *phydev, const u8 *data,
			      size_t size)
{
	int i, ret;
	u16 val;

	ret = phy_modify_mmd(phydev, MDIO_MMD_VEND1, VEND1_GLB_REG_CPU_CTRL,
			     VEND1_GLB_CPU_CTRL_MASK, AEON_CPU_CTRL_FW_LOAD);
	if (ret)
		return ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_FW_START_ADDR,
			    AEON_BOOT_ADDR);
	if (ret)
		return ret;

	ret = phy_modify_mmd(phydev, MDIO_MMD_VEND1,
			     VEND1_GLB_REG_MDIO_INDIRECT_ADDRCMD,
			     0x3ffc, 0xc000);
	if (ret)
		return ret;

	val = phy_read_mmd(phydev, MDIO_MMD_VEND1,
			   VEND1_GLB_REG_MDIO_INDIRECT_STATUS);
	if (val > 1) {
		phydev_err(phydev, "wrong origin mdio_indirect_status: %x\n", val);
		return -EINVAL;
	}

	/* Firmware is always aligned to u16 */
	for (i = 0; i < size; i += 2) {
		val = data[i + 1] << 8 | data[i];

		ret = phy_write_mmd(phydev, MDIO_MMD_VEND1,
				    VEND1_GLB_REG_MDIO_INDIRECT_LOAD, val);
		if (ret)
			return ret;
	}

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1,
			    VEND1_GLB_REG_CPU_RESET_ADDR_LO_BASEADDR,
			    lower_16_bits(AEON_CPU_BOOT_ADDR));
	if (ret)
		return ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1,
			    VEND1_GLB_REG_CPU_RESET_ADDR_HI_BASEADDR,
			    upper_16_bits(AEON_CPU_BOOT_ADDR));
	if (ret)
		return ret;

	return phy_modify_mmd(phydev, MDIO_MMD_VEND1, VEND1_GLB_REG_CPU_CTRL,
			      VEND1_GLB_CPU_CTRL_MASK, AEON_CPU_CTRL_FW_START);
}

static int aeon_firmware_load(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	const struct firmware *fw;
	const char *fw_name;
	int ret;

	ret = of_property_read_string(dev->of_node, "firmware-name",
				      &fw_name);
	if (ret)
		return ret;

	ret = request_firmware(&fw, fw_name, dev);
	if (ret) {
		phydev_err(phydev, "failed to find FW file %s (%d)\n",
			   fw_name, ret);
		return ret;
	}

	ret = aeon_firmware_boot(phydev, fw->data, fw->size);

	release_firmware(fw);

	return ret;
}

static bool aeon_ipc_ready(u16 val, bool parity_status)
{
	u16 status;

	if (FIELD_GET(AEON_IPC_STS_PARITY, val) != parity_status)
		return false;

	status = val & AEON_IPC_STS_STATUS;

	return status != AEON_IPC_STS_STATUS_RCVD &&
	       status != AEON_IPC_STS_STATUS_PROCESS &&
	       status != AEON_IPC_STS_STATUS_BUSY;
}

static int aeon_ipc_wait_cmd(struct phy_device *phydev, bool parity_status)
{
	u16 val;

	/* Exit condition logic:
	 * - Wait for parity bit equal
	 * - Wait for status success, error OR ready
	 */
	return phy_read_mmd_poll_timeout(phydev, MDIO_MMD_VEND1, VEND1_IPC_STS, val,
					 aeon_ipc_ready(val, parity_status),
					 AEON_IPC_DELAY, AEON_IPC_TIMEOUT, false);
}

static int aeon_ipc_send_cmd(struct phy_device *phydev,
			     struct as21xxx_priv *priv,
			     u16 cmd, u16 *ret_sts)
{
	bool curr_parity;
	int ret;

	/* The IPC sync by using a single parity bit.
	 * Each CMD have alternately this bit set or clear
	 * to understand correct flow and packet order.
	 */
	curr_parity = priv->parity_status;
	if (priv->parity_status)
		cmd |= AEON_IPC_CMD_PARITY;

	/* Always update parity for next packet */
	priv->parity_status = !priv->parity_status;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_IPC_CMD, cmd);
	if (ret)
		return ret;

	/* Wait for packet to be processed */
	usleep_range(AEON_IPC_DELAY, AEON_IPC_DELAY + 5000);

	/* With no ret_sts, ignore waiting for packet completion
	 * (ipc parity bit sync)
	 */
	if (!ret_sts)
		return 0;

	ret = aeon_ipc_wait_cmd(phydev, curr_parity);
	if (ret)
		return ret;

	ret = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_IPC_STS);
	if (ret < 0)
		return ret;

	*ret_sts = ret;
	if ((*ret_sts & AEON_IPC_STS_STATUS) != AEON_IPC_STS_STATUS_SUCCESS)
		return -EINVAL;

	return 0;
}

/* If data is NULL, return 0 or negative error.
 * If data not NULL, return number of Bytes received from IPC or
 * a negative error.
 */
static int aeon_ipc_send_msg(struct phy_device *phydev,
			     u16 opcode, u16 *data, unsigned int data_len,
			     u16 *ret_data)
{
	struct as21xxx_priv *priv = phydev->priv;
	unsigned int ret_size;
	u16 cmd, ret_sts;
	int ret;
	int i;

	/* IPC have a max of 8 register to transfer data,
	 * make sure we never exceed this.
	 */
	if (data_len > AEON_IPC_DATA_MAX)
		return -EINVAL;

	for (i = 0; i < data_len / sizeof(u16); i++)
		phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_IPC_DATA(i),
			      data[i]);

	cmd = FIELD_PREP(AEON_IPC_CMD_SIZE, data_len) |
	      FIELD_PREP(AEON_IPC_CMD_OPCODE, opcode);

	mutex_lock(&priv->ipc_lock);

	ret = aeon_ipc_send_cmd(phydev, priv, cmd, &ret_sts);
	if (ret) {
		phydev_err(phydev, "failed to send ipc msg for %x: %d\n",
			   opcode, ret);
		goto out;
	}

	if (!data)
		goto out;

	if ((ret_sts & AEON_IPC_STS_STATUS) == AEON_IPC_STS_STATUS_ERROR) {
		ret = -EINVAL;
		goto out;
	}

	/* Prevent IPC from stack smashing the kernel.
	 * We can't trust IPC to return a good value and we always
	 * preallocate space for 16 Bytes.
	 */
	ret_size = FIELD_GET(AEON_IPC_STS_SIZE, ret_sts);
	if (ret_size > AEON_IPC_DATA_MAX) {
		ret = -EINVAL;
		goto out;
	}

	/* Read data from IPC data register for ret_size value from IPC */
	for (i = 0; i < DIV_ROUND_UP(ret_size, sizeof(u16)); i++) {
		ret = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_IPC_DATA(i));
		if (ret < 0)
			goto out;

		ret_data[i] = ret;
	}

	ret = ret_size;

out:
	mutex_unlock(&priv->ipc_lock);

	return ret;
}

static int aeon_ipc_noop(struct phy_device *phydev,
			 struct as21xxx_priv *priv, u16 *ret_sts)
{
	u16 cmd;

	cmd = FIELD_PREP(AEON_IPC_CMD_SIZE, 0) |
	      FIELD_PREP(AEON_IPC_CMD_OPCODE, IPC_CMD_NOOP);

	return aeon_ipc_send_cmd(phydev, priv, cmd, ret_sts);
}

/* Logic to sync parity bit with IPC.
 * We send 2 NOP cmd with same partity and we wait for IPC
 * to handle the packet only for the second one. This way
 * we make sure we are sync for every next cmd.
 */
static int aeon_ipc_sync_parity(struct phy_device *phydev,
				struct as21xxx_priv *priv)
{
	u16 ret_sts;
	int ret;

	mutex_lock(&priv->ipc_lock);

	/* Send NOP with no parity */
	aeon_ipc_noop(phydev, priv, NULL);

	/* Reset packet parity */
	priv->parity_status = false;

	/* Send second NOP with no parity */
	ret = aeon_ipc_noop(phydev, priv, &ret_sts);

	mutex_unlock(&priv->ipc_lock);

	/* We expect to return -EINVAL */
	if (ret != -EINVAL)
		return ret;

	if ((ret_sts & AEON_IPC_STS_STATUS) != AEON_IPC_STS_STATUS_READY) {
		phydev_err(phydev, "Invalid IPC status on sync parity: %x\n",
			   ret_sts);
		return -EINVAL;
	}

	return 0;
}

static int aeon_ipc_get_fw_version(struct phy_device *phydev)
{
	u16 ret_data[AEON_IPC_DATA_NUM_REGISTERS], data[1];
	char fw_version[AEON_IPC_DATA_MAX + 1];
	int ret;

	data[0] = IPC_INFO_VERSION;

	ret = aeon_ipc_send_msg(phydev, IPC_CMD_INFO, data,
				sizeof(data), ret_data);
	if (ret < 0)
		return ret;

	/* Make sure FW version is NULL terminated */
	memcpy(fw_version, ret_data, ret);
	fw_version[ret] = '\0';

	phydev_info(phydev, "Firmware Version: %s\n", fw_version);

	return 0;
}

static int aeon_dpc_ra_enable(struct phy_device *phydev)
{
	u16 data[2];

	data[0] = IPC_CFG_PARAM_DIRECT;
	data[1] = IPC_CFG_PARAM_DIRECT_DPC_RA;

	return aeon_ipc_send_msg(phydev, IPC_CMD_CFG_PARAM, data,
				 sizeof(data), NULL);
}

static int as21xxx_probe(struct phy_device *phydev)
{
	struct as21xxx_priv *priv;
	int ret;

	priv = devm_kzalloc(&phydev->mdio.dev,
			    sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	phydev->priv = priv;

	ret = devm_mutex_init(&phydev->mdio.dev,
			      &priv->ipc_lock);
	if (ret)
		return ret;

	ret = aeon_ipc_sync_parity(phydev, priv);
	if (ret)
		return ret;

	ret = aeon_ipc_get_fw_version(phydev);
	if (ret)
		return ret;

	/* Enable PTP clk if not already Enabled */
	ret = phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_PTP_CLK,
			       VEND1_PTP_CLK_EN);
	if (ret)
		return ret;

	return aeon_dpc_ra_enable(phydev);
}

static int as21xxx_read_link(struct phy_device *phydev, int *bmcr)
{
	int status;

	/* Normal C22 BMCR report inconsistent data, use
	 * the mapped C22 in C45 to have more consistent link info.
	 */
	*bmcr = phy_read_mmd(phydev, MDIO_MMD_AN,
			     AS21XXX_MDIO_AN_C22 + MII_BMCR);
	if (*bmcr < 0)
		return *bmcr;

	/* Autoneg is being started, therefore disregard current
	 * link status and report link as down.
	 */
	if (*bmcr & BMCR_ANRESTART) {
		phydev->link = 0;
		return 0;
	}

	status = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_STAT1);
	if (status < 0)
		return status;

	phydev->link = !!(status & MDIO_STAT1_LSTATUS);

	return 0;
}

static int as21xxx_read_c22_lpa(struct phy_device *phydev)
{
	int lpagb;

	/* MII_STAT1000 are only filled in the mapped C22
	 * in C45, use that to fill lpagb values and check.
	 */
	lpagb = phy_read_mmd(phydev, MDIO_MMD_AN,
			     AS21XXX_MDIO_AN_C22 + MII_STAT1000);
	if (lpagb < 0)
		return lpagb;

	if (lpagb & LPA_1000MSFAIL) {
		int adv = phy_read_mmd(phydev, MDIO_MMD_AN,
				       AS21XXX_MDIO_AN_C22 + MII_CTRL1000);

		if (adv < 0)
			return adv;

		if (adv & CTL1000_ENABLE_MASTER)
			phydev_err(phydev, "Master/Slave resolution failed, maybe conflicting manual settings?\n");
		else
			phydev_err(phydev, "Master/Slave resolution failed\n");
		return -ENOLINK;
	}

	mii_stat1000_mod_linkmode_lpa_t(phydev->lp_advertising,
					lpagb);

	return 0;
}

static int as21xxx_read_status(struct phy_device *phydev)
{
	int bmcr, old_link = phydev->link;
	int ret;

	ret = as21xxx_read_link(phydev, &bmcr);
	if (ret)
		return ret;

	/* why bother the PHY if nothing can have changed */
	if (phydev->autoneg == AUTONEG_ENABLE && old_link && phydev->link)
		return 0;

	phydev->speed = SPEED_UNKNOWN;
	phydev->duplex = DUPLEX_UNKNOWN;
	phydev->pause = 0;
	phydev->asym_pause = 0;

	if (phydev->autoneg == AUTONEG_ENABLE) {
		ret = genphy_c45_read_lpa(phydev);
		if (ret)
			return ret;

		ret = as21xxx_read_c22_lpa(phydev);
		if (ret)
			return ret;

		phy_resolve_aneg_linkmode(phydev);
	} else {
		int speed;

		linkmode_zero(phydev->lp_advertising);

		speed = phy_read_mmd(phydev, MDIO_MMD_VEND1,
				     VEND1_SPEED_STATUS);
		if (speed < 0)
			return speed;

		switch (speed & VEND1_SPEED_STATUS) {
		case VEND1_SPEED_10000:
			phydev->speed = SPEED_10000;
			phydev->duplex = DUPLEX_FULL;
			break;
		case VEND1_SPEED_5000:
			phydev->speed = SPEED_5000;
			phydev->duplex = DUPLEX_FULL;
			break;
		case VEND1_SPEED_2500:
			phydev->speed = SPEED_2500;
			phydev->duplex = DUPLEX_FULL;
			break;
		case VEND1_SPEED_1000:
			phydev->speed = SPEED_1000;
			if (bmcr & BMCR_FULLDPLX)
				phydev->duplex = DUPLEX_FULL;
			else
				phydev->duplex = DUPLEX_HALF;
			break;
		case VEND1_SPEED_100:
			phydev->speed = SPEED_100;
			phydev->duplex = DUPLEX_FULL;
			break;
		case VEND1_SPEED_10:
			phydev->speed = SPEED_10;
			phydev->duplex = DUPLEX_FULL;
			break;
		default:
			return -EINVAL;
		}
	}

	return 0;
}

static int as21xxx_led_brightness_set(struct phy_device *phydev,
				      u8 index, enum led_brightness value)
{
	u16 val = VEND1_LED_REG_A_EVENT_OFF;

	if (index > AEON_MAX_LEDS)
		return -EINVAL;

	if (value)
		val = VEND1_LED_REG_A_EVENT_ON;

	return phy_modify_mmd(phydev, MDIO_MMD_VEND1,
			      VEND1_LED_REG(index),
			      VEND1_LED_REG_A_EVENT,
			      FIELD_PREP(VEND1_LED_REG_A_EVENT, val));
}

static int as21xxx_led_hw_is_supported(struct phy_device *phydev, u8 index,
				       unsigned long rules)
{
	int i;

	if (index > AEON_MAX_LEDS)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(as21xxx_led_supported_pattern); i++)
		if (rules == as21xxx_led_supported_pattern[i].pattern)
			return 0;

	return -EOPNOTSUPP;
}

static int as21xxx_led_hw_control_get(struct phy_device *phydev, u8 index,
				      unsigned long *rules)
{
	int i, val;

	if (index > AEON_MAX_LEDS)
		return -EINVAL;

	val = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_LED_REG(index));
	if (val < 0)
		return val;

	val = FIELD_GET(VEND1_LED_REG_A_EVENT, val);
	for (i = 0; i < ARRAY_SIZE(as21xxx_led_supported_pattern); i++)
		if (val == as21xxx_led_supported_pattern[i].val) {
			*rules = as21xxx_led_supported_pattern[i].pattern;
			return 0;
		}

	/* Should be impossible */
	return -EINVAL;
}

static int as21xxx_led_hw_control_set(struct phy_device *phydev, u8 index,
				      unsigned long rules)
{
	u16 val = 0;
	int i;

	if (index > AEON_MAX_LEDS)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(as21xxx_led_supported_pattern); i++)
		if (rules == as21xxx_led_supported_pattern[i].pattern) {
			val = as21xxx_led_supported_pattern[i].val;
			break;
		}

	return phy_modify_mmd(phydev, MDIO_MMD_VEND1,
			      VEND1_LED_REG(index),
			      VEND1_LED_REG_A_EVENT,
			      FIELD_PREP(VEND1_LED_REG_A_EVENT, val));
}

static int as21xxx_led_polarity_set(struct phy_device *phydev, int index,
				    unsigned long modes)
{
	bool led_active_low = false;
	u16 mask, val = 0;
	u32 mode;

	if (index > AEON_MAX_LEDS)
		return -EINVAL;

	for_each_set_bit(mode, &modes, __PHY_LED_MODES_NUM) {
		switch (mode) {
		case PHY_LED_ACTIVE_LOW:
			led_active_low = true;
			break;
		case PHY_LED_ACTIVE_HIGH: /* default mode */
			led_active_low = false;
			break;
		default:
			return -EINVAL;
		}
	}

	mask = VEND1_GLB_CPU_CTRL_LED_POLARITY(index);
	if (led_active_low)
		val = VEND1_GLB_CPU_CTRL_LED_POLARITY(index);

	return phy_modify_mmd(phydev, MDIO_MMD_VEND1,
			      VEND1_GLB_REG_CPU_CTRL,
			      mask, val);
}

static int as21xxx_match_phy_device(struct phy_device *phydev,
				    const struct phy_driver *phydrv)
{
	struct as21xxx_priv *priv;
	u16 ret_sts;
	u32 phy_id;
	int ret;

	/* Skip PHY that are not AS21xxx */
	if (!phy_id_compare_vendor(phydev->c45_ids.device_ids[MDIO_MMD_PCS],
				   PHY_VENDOR_AEONSEMI))
		return genphy_match_phy_device(phydev, phydrv);

	/* Read PHY ID to handle firmware loaded or HW reset */
	ret = phy_read_mmd(phydev, MDIO_MMD_PCS, MII_PHYSID1);
	if (ret < 0)
		return ret;
	phy_id = ret << 16;

	ret = phy_read_mmd(phydev, MDIO_MMD_PCS, MII_PHYSID2);
	if (ret < 0)
		return ret;
	phy_id |= ret;

	/* With PHY ID not the generic AS21xxx one assume
	 * the firmware just loaded
	 */
	if (phy_id != PHY_ID_AS21XXX)
		return phy_id == phydrv->phy_id;

	/* Allocate temp priv and load the firmware */
	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mutex_init(&priv->ipc_lock);

	ret = aeon_firmware_load(phydev);
	if (ret)
		goto out;

	/* Sync parity... */
	ret = aeon_ipc_sync_parity(phydev, priv);
	if (ret)
		goto out;

	/* ...and send a third NOOP cmd to wait for firmware finish loading */
	ret = aeon_ipc_noop(phydev, priv, &ret_sts);
	if (ret)
		goto out;

out:
	mutex_destroy(&priv->ipc_lock);
	kfree(priv);

	/* Return can either be 0 or a negative error code.
	 * Returning 0 here means THIS is NOT a suitable PHY.
	 *
	 * For the specific case of the generic Aeonsemi PHY ID that
	 * needs the firmware the be loaded first to have a correct PHY ID,
	 * this is OK as a matching PHY ID will be found right after.
	 * This relies on the driver probe order where the first PHY driver
	 * probed is the generic one.
	 */
	return ret;
}

static struct phy_driver as21xxx_drivers[] = {
	{
		/* PHY expose in C45 as 0x7500 0x9410
		 * before firmware is loaded.
		 * This driver entry must be attempted first to load
		 * the firmware and thus update the ID registers.
		 */
		PHY_ID_MATCH_EXACT(PHY_ID_AS21XXX),
		.name		= "Aeonsemi AS21xxx",
		.match_phy_device = as21xxx_match_phy_device,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21011JB1),
		.name		= "Aeonsemi AS21011JB1",
		.probe		= as21xxx_probe,
		.match_phy_device = as21xxx_match_phy_device,
		.read_status	= as21xxx_read_status,
		.led_brightness_set = as21xxx_led_brightness_set,
		.led_hw_is_supported = as21xxx_led_hw_is_supported,
		.led_hw_control_set = as21xxx_led_hw_control_set,
		.led_hw_control_get = as21xxx_led_hw_control_get,
		.led_polarity_set = as21xxx_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21011PB1),
		.name		= "Aeonsemi AS21011PB1",
		.probe		= as21xxx_probe,
		.match_phy_device = as21xxx_match_phy_device,
		.read_status	= as21xxx_read_status,
		.led_brightness_set = as21xxx_led_brightness_set,
		.led_hw_is_supported = as21xxx_led_hw_is_supported,
		.led_hw_control_set = as21xxx_led_hw_control_set,
		.led_hw_control_get = as21xxx_led_hw_control_get,
		.led_polarity_set = as21xxx_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21010PB1),
		.name		= "Aeonsemi AS21010PB1",
		.probe		= as21xxx_probe,
		.match_phy_device = as21xxx_match_phy_device,
		.read_status	= as21xxx_read_status,
		.led_brightness_set = as21xxx_led_brightness_set,
		.led_hw_is_supported = as21xxx_led_hw_is_supported,
		.led_hw_control_set = as21xxx_led_hw_control_set,
		.led_hw_control_get = as21xxx_led_hw_control_get,
		.led_polarity_set = as21xxx_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21010JB1),
		.name		= "Aeonsemi AS21010JB1",
		.probe		= as21xxx_probe,
		.match_phy_device = as21xxx_match_phy_device,
		.read_status	= as21xxx_read_status,
		.led_brightness_set = as21xxx_led_brightness_set,
		.led_hw_is_supported = as21xxx_led_hw_is_supported,
		.led_hw_control_set = as21xxx_led_hw_control_set,
		.led_hw_control_get = as21xxx_led_hw_control_get,
		.led_polarity_set = as21xxx_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21210PB1),
		.name		= "Aeonsemi AS21210PB1",
		.probe		= as21xxx_probe,
		.match_phy_device = as21xxx_match_phy_device,
		.read_status	= as21xxx_read_status,
		.led_brightness_set = as21xxx_led_brightness_set,
		.led_hw_is_supported = as21xxx_led_hw_is_supported,
		.led_hw_control_set = as21xxx_led_hw_control_set,
		.led_hw_control_get = as21xxx_led_hw_control_get,
		.led_polarity_set = as21xxx_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21510JB1),
		.name		= "Aeonsemi AS21510JB1",
		.probe		= as21xxx_probe,
		.match_phy_device = as21xxx_match_phy_device,
		.read_status	= as21xxx_read_status,
		.led_brightness_set = as21xxx_led_brightness_set,
		.led_hw_is_supported = as21xxx_led_hw_is_supported,
		.led_hw_control_set = as21xxx_led_hw_control_set,
		.led_hw_control_get = as21xxx_led_hw_control_get,
		.led_polarity_set = as21xxx_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21510PB1),
		.name		= "Aeonsemi AS21510PB1",
		.probe		= as21xxx_probe,
		.match_phy_device = as21xxx_match_phy_device,
		.read_status	= as21xxx_read_status,
		.led_brightness_set = as21xxx_led_brightness_set,
		.led_hw_is_supported = as21xxx_led_hw_is_supported,
		.led_hw_control_set = as21xxx_led_hw_control_set,
		.led_hw_control_get = as21xxx_led_hw_control_get,
		.led_polarity_set = as21xxx_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21511JB1),
		.name		= "Aeonsemi AS21511JB1",
		.probe		= as21xxx_probe,
		.match_phy_device = as21xxx_match_phy_device,
		.read_status	= as21xxx_read_status,
		.led_brightness_set = as21xxx_led_brightness_set,
		.led_hw_is_supported = as21xxx_led_hw_is_supported,
		.led_hw_control_set = as21xxx_led_hw_control_set,
		.led_hw_control_get = as21xxx_led_hw_control_get,
		.led_polarity_set = as21xxx_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21210JB1),
		.name		= "Aeonsemi AS21210JB1",
		.probe		= as21xxx_probe,
		.match_phy_device = as21xxx_match_phy_device,
		.read_status	= as21xxx_read_status,
		.led_brightness_set = as21xxx_led_brightness_set,
		.led_hw_is_supported = as21xxx_led_hw_is_supported,
		.led_hw_control_set = as21xxx_led_hw_control_set,
		.led_hw_control_get = as21xxx_led_hw_control_get,
		.led_polarity_set = as21xxx_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21511PB1),
		.name		= "Aeonsemi AS21511PB1",
		.probe		= as21xxx_probe,
		.match_phy_device = as21xxx_match_phy_device,
		.read_status	= as21xxx_read_status,
		.led_brightness_set = as21xxx_led_brightness_set,
		.led_hw_is_supported = as21xxx_led_hw_is_supported,
		.led_hw_control_set = as21xxx_led_hw_control_set,
		.led_hw_control_get = as21xxx_led_hw_control_get,
		.led_polarity_set = as21xxx_led_polarity_set,
	},
};
module_phy_driver(as21xxx_drivers);

static struct mdio_device_id __maybe_unused as21xxx_tbl[] = {
	{ PHY_ID_MATCH_VENDOR(PHY_VENDOR_AEONSEMI) },
	{ }
};
MODULE_DEVICE_TABLE(mdio, as21xxx_tbl);

MODULE_DESCRIPTION("Aeonsemi AS21xxx PHY driver");
MODULE_AUTHOR("Christian Marangi <ansuelsmth@gmail.com>");
MODULE_LICENSE("GPL");
