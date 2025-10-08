// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for the Airoha EN8811H 2.5 Gigabit PHY.
 *
 * Limitations of the EN8811H:
 * - Only full duplex supported
 * - Forced speed (AN off) is not supported by hardware (100Mbps)
 *
 * Source originated from airoha's en8811h.c and en8811h.h v1.2.1
 *
 * Copyright (C) 2023 Airoha Technology Corp.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/phy.h>
#include <linux/firmware.h>
#include <linux/property.h>
#include <linux/wordpart.h>
#include <linux/unaligned.h>

#define EN8811H_PHY_ID		0x03a2a411

#define EN8811H_MD32_DM		"airoha/EthMD32.dm.bin"
#define EN8811H_MD32_DSP	"airoha/EthMD32.DSP.bin"

#define AIR_FW_ADDR_DM	0x00000000
#define AIR_FW_ADDR_DSP	0x00100000

/* MII Registers */
#define AIR_AUX_CTRL_STATUS		0x1d
#define   AIR_AUX_CTRL_STATUS_SPEED_MASK	GENMASK(4, 2)
#define   AIR_AUX_CTRL_STATUS_SPEED_100		0x4
#define   AIR_AUX_CTRL_STATUS_SPEED_1000	0x8
#define   AIR_AUX_CTRL_STATUS_SPEED_2500	0xc

#define AIR_EXT_PAGE_ACCESS		0x1f
#define   AIR_PHY_PAGE_STANDARD			0x0000
#define   AIR_PHY_PAGE_EXTENDED_4		0x0004

/* MII Registers Page 4*/
#define AIR_BPBUS_MODE			0x10
#define   AIR_BPBUS_MODE_ADDR_FIXED		0x0000
#define   AIR_BPBUS_MODE_ADDR_INCR		BIT(15)
#define AIR_BPBUS_WR_ADDR_HIGH		0x11
#define AIR_BPBUS_WR_ADDR_LOW		0x12
#define AIR_BPBUS_WR_DATA_HIGH		0x13
#define AIR_BPBUS_WR_DATA_LOW		0x14
#define AIR_BPBUS_RD_ADDR_HIGH		0x15
#define AIR_BPBUS_RD_ADDR_LOW		0x16
#define AIR_BPBUS_RD_DATA_HIGH		0x17
#define AIR_BPBUS_RD_DATA_LOW		0x18

/* Registers on MDIO_MMD_VEND1 */
#define EN8811H_PHY_FW_STATUS		0x8009
#define   EN8811H_PHY_READY			0x02

#define AIR_PHY_MCU_CMD_1		0x800c
#define AIR_PHY_MCU_CMD_1_MODE1			0x0
#define AIR_PHY_MCU_CMD_2		0x800d
#define AIR_PHY_MCU_CMD_2_MODE1			0x0
#define AIR_PHY_MCU_CMD_3		0x800e
#define AIR_PHY_MCU_CMD_3_MODE1			0x1101
#define AIR_PHY_MCU_CMD_3_DOCMD			0x1100
#define AIR_PHY_MCU_CMD_4		0x800f
#define AIR_PHY_MCU_CMD_4_MODE1			0x0002
#define AIR_PHY_MCU_CMD_4_INTCLR		0x00e4

/* Registers on MDIO_MMD_VEND2 */
#define AIR_PHY_LED_BCR			0x021
#define   AIR_PHY_LED_BCR_MODE_MASK		GENMASK(1, 0)
#define   AIR_PHY_LED_BCR_TIME_TEST		BIT(2)
#define   AIR_PHY_LED_BCR_CLK_EN		BIT(3)
#define   AIR_PHY_LED_BCR_EXT_CTRL		BIT(15)

#define AIR_PHY_LED_DUR_ON		0x022

#define AIR_PHY_LED_DUR_BLINK		0x023

#define AIR_PHY_LED_ON(i)	       (0x024 + ((i) * 2))
#define   AIR_PHY_LED_ON_MASK			(GENMASK(6, 0) | BIT(8))
#define   AIR_PHY_LED_ON_LINK1000		BIT(0)
#define   AIR_PHY_LED_ON_LINK100		BIT(1)
#define   AIR_PHY_LED_ON_LINK10			BIT(2)
#define   AIR_PHY_LED_ON_LINKDOWN		BIT(3)
#define   AIR_PHY_LED_ON_FDX			BIT(4) /* Full duplex */
#define   AIR_PHY_LED_ON_HDX			BIT(5) /* Half duplex */
#define   AIR_PHY_LED_ON_FORCE_ON		BIT(6)
#define   AIR_PHY_LED_ON_LINK2500		BIT(8)
#define   AIR_PHY_LED_ON_POLARITY		BIT(14)
#define   AIR_PHY_LED_ON_ENABLE			BIT(15)

#define AIR_PHY_LED_BLINK(i)	       (0x025 + ((i) * 2))
#define   AIR_PHY_LED_BLINK_1000TX		BIT(0)
#define   AIR_PHY_LED_BLINK_1000RX		BIT(1)
#define   AIR_PHY_LED_BLINK_100TX		BIT(2)
#define   AIR_PHY_LED_BLINK_100RX		BIT(3)
#define   AIR_PHY_LED_BLINK_10TX		BIT(4)
#define   AIR_PHY_LED_BLINK_10RX		BIT(5)
#define   AIR_PHY_LED_BLINK_COLLISION		BIT(6)
#define   AIR_PHY_LED_BLINK_RX_CRC_ERR		BIT(7)
#define   AIR_PHY_LED_BLINK_RX_IDLE_ERR		BIT(8)
#define   AIR_PHY_LED_BLINK_FORCE_BLINK		BIT(9)
#define   AIR_PHY_LED_BLINK_2500TX		BIT(10)
#define   AIR_PHY_LED_BLINK_2500RX		BIT(11)

/* Registers on BUCKPBUS */
#define EN8811H_2P5G_LPA		0x3b30
#define   EN8811H_2P5G_LPA_2P5G			BIT(0)

#define EN8811H_FW_VERSION		0x3b3c

#define EN8811H_POLARITY		0xca0f8
#define   EN8811H_POLARITY_TX_NORMAL		BIT(0)
#define   EN8811H_POLARITY_RX_REVERSE		BIT(1)

#define EN8811H_GPIO_OUTPUT		0xcf8b8
#define   EN8811H_GPIO_OUTPUT_345		(BIT(3) | BIT(4) | BIT(5))

#define EN8811H_HWTRAP1			0xcf914
#define   EN8811H_HWTRAP1_CKO			BIT(12)
#define EN8811H_CLK_CGM			0xcf958
#define   EN8811H_CLK_CGM_CKO			BIT(26)

#define EN8811H_FW_CTRL_1		0x0f0018
#define   EN8811H_FW_CTRL_1_START		0x0
#define   EN8811H_FW_CTRL_1_FINISH		0x1
#define EN8811H_FW_CTRL_2		0x800000
#define EN8811H_FW_CTRL_2_LOADING		BIT(11)

/* Led definitions */
#define EN8811H_LED_COUNT	3

/* Default LED setup:
 * GPIO5 <-> LED0  On: Link detected, blink Rx/Tx
 * GPIO4 <-> LED1  On: Link detected at 2500 or 1000 Mbps
 * GPIO3 <-> LED2  On: Link detected at 2500 or  100 Mbps
 */
#define AIR_DEFAULT_TRIGGER_LED0 (BIT(TRIGGER_NETDEV_LINK)      | \
				  BIT(TRIGGER_NETDEV_RX)        | \
				  BIT(TRIGGER_NETDEV_TX))
#define AIR_DEFAULT_TRIGGER_LED1 (BIT(TRIGGER_NETDEV_LINK_2500) | \
				  BIT(TRIGGER_NETDEV_LINK_1000))
#define AIR_DEFAULT_TRIGGER_LED2 (BIT(TRIGGER_NETDEV_LINK_2500) | \
				  BIT(TRIGGER_NETDEV_LINK_100))

struct led {
	unsigned long rules;
	unsigned long state;
};

#define clk_hw_to_en8811h_priv(_hw)			\
	container_of(_hw, struct en8811h_priv, hw)

struct en8811h_priv {
	u32			firmware_version;
	bool			mcu_needs_restart;
	struct led		led[EN8811H_LED_COUNT];
	struct clk_hw		hw;
	struct phy_device	*phydev;
	unsigned int		cko_is_enabled;
};

enum {
	AIR_PHY_LED_STATE_FORCE_ON,
	AIR_PHY_LED_STATE_FORCE_BLINK,
};

enum {
	AIR_PHY_LED_DUR_BLINK_32MS,
	AIR_PHY_LED_DUR_BLINK_64MS,
	AIR_PHY_LED_DUR_BLINK_128MS,
	AIR_PHY_LED_DUR_BLINK_256MS,
	AIR_PHY_LED_DUR_BLINK_512MS,
	AIR_PHY_LED_DUR_BLINK_1024MS,
};

enum {
	AIR_LED_DISABLE,
	AIR_LED_ENABLE,
};

enum {
	AIR_ACTIVE_LOW,
	AIR_ACTIVE_HIGH,
};

enum {
	AIR_LED_MODE_DISABLE,
	AIR_LED_MODE_USER_DEFINE,
};

#define AIR_PHY_LED_DUR_UNIT	1024
#define AIR_PHY_LED_DUR (AIR_PHY_LED_DUR_UNIT << AIR_PHY_LED_DUR_BLINK_64MS)

static const unsigned long en8811h_led_trig = BIT(TRIGGER_NETDEV_FULL_DUPLEX) |
					      BIT(TRIGGER_NETDEV_LINK)        |
					      BIT(TRIGGER_NETDEV_LINK_10)     |
					      BIT(TRIGGER_NETDEV_LINK_100)    |
					      BIT(TRIGGER_NETDEV_LINK_1000)   |
					      BIT(TRIGGER_NETDEV_LINK_2500)   |
					      BIT(TRIGGER_NETDEV_RX)          |
					      BIT(TRIGGER_NETDEV_TX);

static int air_phy_read_page(struct phy_device *phydev)
{
	return __phy_read(phydev, AIR_EXT_PAGE_ACCESS);
}

static int air_phy_write_page(struct phy_device *phydev, int page)
{
	return __phy_write(phydev, AIR_EXT_PAGE_ACCESS, page);
}

static int __air_buckpbus_reg_write(struct phy_device *phydev,
				    u32 pbus_address, u32 pbus_data)
{
	int ret;

	ret = __phy_write(phydev, AIR_BPBUS_MODE, AIR_BPBUS_MODE_ADDR_FIXED);
	if (ret < 0)
		return ret;

	ret = __phy_write(phydev, AIR_BPBUS_WR_ADDR_HIGH,
			  upper_16_bits(pbus_address));
	if (ret < 0)
		return ret;

	ret = __phy_write(phydev, AIR_BPBUS_WR_ADDR_LOW,
			  lower_16_bits(pbus_address));
	if (ret < 0)
		return ret;

	ret = __phy_write(phydev, AIR_BPBUS_WR_DATA_HIGH,
			  upper_16_bits(pbus_data));
	if (ret < 0)
		return ret;

	ret = __phy_write(phydev, AIR_BPBUS_WR_DATA_LOW,
			  lower_16_bits(pbus_data));
	if (ret < 0)
		return ret;

	return 0;
}

static int air_buckpbus_reg_write(struct phy_device *phydev,
				  u32 pbus_address, u32 pbus_data)
{
	int saved_page;
	int ret = 0;

	saved_page = phy_select_page(phydev, AIR_PHY_PAGE_EXTENDED_4);

	if (saved_page >= 0) {
		ret = __air_buckpbus_reg_write(phydev, pbus_address,
					       pbus_data);
		if (ret < 0)
			phydev_err(phydev, "%s 0x%08x failed: %d\n", __func__,
				   pbus_address, ret);
	}

	return phy_restore_page(phydev, saved_page, ret);
}

static int __air_buckpbus_reg_read(struct phy_device *phydev,
				   u32 pbus_address, u32 *pbus_data)
{
	int pbus_data_low, pbus_data_high;
	int ret;

	ret = __phy_write(phydev, AIR_BPBUS_MODE, AIR_BPBUS_MODE_ADDR_FIXED);
	if (ret < 0)
		return ret;

	ret = __phy_write(phydev, AIR_BPBUS_RD_ADDR_HIGH,
			  upper_16_bits(pbus_address));
	if (ret < 0)
		return ret;

	ret = __phy_write(phydev, AIR_BPBUS_RD_ADDR_LOW,
			  lower_16_bits(pbus_address));
	if (ret < 0)
		return ret;

	pbus_data_high = __phy_read(phydev, AIR_BPBUS_RD_DATA_HIGH);
	if (pbus_data_high < 0)
		return pbus_data_high;

	pbus_data_low = __phy_read(phydev, AIR_BPBUS_RD_DATA_LOW);
	if (pbus_data_low < 0)
		return pbus_data_low;

	*pbus_data = pbus_data_low | (pbus_data_high << 16);
	return 0;
}

static int air_buckpbus_reg_read(struct phy_device *phydev,
				 u32 pbus_address, u32 *pbus_data)
{
	int saved_page;
	int ret = 0;

	saved_page = phy_select_page(phydev, AIR_PHY_PAGE_EXTENDED_4);

	if (saved_page >= 0) {
		ret = __air_buckpbus_reg_read(phydev, pbus_address, pbus_data);
		if (ret < 0)
			phydev_err(phydev, "%s 0x%08x failed: %d\n", __func__,
				   pbus_address, ret);
	}

	return phy_restore_page(phydev, saved_page, ret);
}

static int __air_buckpbus_reg_modify(struct phy_device *phydev,
				     u32 pbus_address, u32 mask, u32 set)
{
	int pbus_data_low, pbus_data_high;
	u32 pbus_data_old, pbus_data_new;
	int ret;

	ret = __phy_write(phydev, AIR_BPBUS_MODE, AIR_BPBUS_MODE_ADDR_FIXED);
	if (ret < 0)
		return ret;

	ret = __phy_write(phydev, AIR_BPBUS_RD_ADDR_HIGH,
			  upper_16_bits(pbus_address));
	if (ret < 0)
		return ret;

	ret = __phy_write(phydev, AIR_BPBUS_RD_ADDR_LOW,
			  lower_16_bits(pbus_address));
	if (ret < 0)
		return ret;

	pbus_data_high = __phy_read(phydev, AIR_BPBUS_RD_DATA_HIGH);
	if (pbus_data_high < 0)
		return pbus_data_high;

	pbus_data_low = __phy_read(phydev, AIR_BPBUS_RD_DATA_LOW);
	if (pbus_data_low < 0)
		return pbus_data_low;

	pbus_data_old = pbus_data_low | (pbus_data_high << 16);
	pbus_data_new = (pbus_data_old & ~mask) | set;
	if (pbus_data_new == pbus_data_old)
		return 0;

	ret = __phy_write(phydev, AIR_BPBUS_WR_ADDR_HIGH,
			  upper_16_bits(pbus_address));
	if (ret < 0)
		return ret;

	ret = __phy_write(phydev, AIR_BPBUS_WR_ADDR_LOW,
			  lower_16_bits(pbus_address));
	if (ret < 0)
		return ret;

	ret = __phy_write(phydev, AIR_BPBUS_WR_DATA_HIGH,
			  upper_16_bits(pbus_data_new));
	if (ret < 0)
		return ret;

	ret = __phy_write(phydev, AIR_BPBUS_WR_DATA_LOW,
			  lower_16_bits(pbus_data_new));
	if (ret < 0)
		return ret;

	return 0;
}

static int air_buckpbus_reg_modify(struct phy_device *phydev,
				   u32 pbus_address, u32 mask, u32 set)
{
	int saved_page;
	int ret = 0;

	saved_page = phy_select_page(phydev, AIR_PHY_PAGE_EXTENDED_4);

	if (saved_page >= 0) {
		ret = __air_buckpbus_reg_modify(phydev, pbus_address, mask,
						set);
		if (ret < 0)
			phydev_err(phydev, "%s 0x%08x failed: %d\n", __func__,
				   pbus_address, ret);
	}

	return phy_restore_page(phydev, saved_page, ret);
}

static int __air_write_buf(struct phy_device *phydev, u32 address,
			   const struct firmware *fw)
{
	unsigned int offset;
	int ret;
	u16 val;

	ret = __phy_write(phydev, AIR_BPBUS_MODE, AIR_BPBUS_MODE_ADDR_INCR);
	if (ret < 0)
		return ret;

	ret = __phy_write(phydev, AIR_BPBUS_WR_ADDR_HIGH,
			  upper_16_bits(address));
	if (ret < 0)
		return ret;

	ret = __phy_write(phydev, AIR_BPBUS_WR_ADDR_LOW,
			  lower_16_bits(address));
	if (ret < 0)
		return ret;

	for (offset = 0; offset < fw->size; offset += 4) {
		val = get_unaligned_le16(&fw->data[offset + 2]);
		ret = __phy_write(phydev, AIR_BPBUS_WR_DATA_HIGH, val);
		if (ret < 0)
			return ret;

		val = get_unaligned_le16(&fw->data[offset]);
		ret = __phy_write(phydev, AIR_BPBUS_WR_DATA_LOW, val);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int air_write_buf(struct phy_device *phydev, u32 address,
			 const struct firmware *fw)
{
	int saved_page;
	int ret = 0;

	saved_page = phy_select_page(phydev, AIR_PHY_PAGE_EXTENDED_4);

	if (saved_page >= 0) {
		ret = __air_write_buf(phydev, address, fw);
		if (ret < 0)
			phydev_err(phydev, "%s 0x%08x failed: %d\n", __func__,
				   address, ret);
	}

	return phy_restore_page(phydev, saved_page, ret);
}

static int en8811h_wait_mcu_ready(struct phy_device *phydev)
{
	int ret, reg_value;

	/* Because of mdio-lock, may have to wait for multiple loads */
	ret = phy_read_mmd_poll_timeout(phydev, MDIO_MMD_VEND1,
					EN8811H_PHY_FW_STATUS, reg_value,
					reg_value == EN8811H_PHY_READY,
					20000, 7500000, true);
	if (ret) {
		phydev_err(phydev, "MCU not ready: 0x%x\n", reg_value);
		return -ENODEV;
	}

	return 0;
}

static int en8811h_load_firmware(struct phy_device *phydev)
{
	struct en8811h_priv *priv = phydev->priv;
	struct device *dev = &phydev->mdio.dev;
	const struct firmware *fw1, *fw2;
	int ret;

	ret = request_firmware_direct(&fw1, EN8811H_MD32_DM, dev);
	if (ret < 0)
		return ret;

	ret = request_firmware_direct(&fw2, EN8811H_MD32_DSP, dev);
	if (ret < 0)
		goto en8811h_load_firmware_rel1;

	ret = air_buckpbus_reg_write(phydev, EN8811H_FW_CTRL_1,
				     EN8811H_FW_CTRL_1_START);
	if (ret < 0)
		goto en8811h_load_firmware_out;

	ret = air_buckpbus_reg_modify(phydev, EN8811H_FW_CTRL_2,
				      EN8811H_FW_CTRL_2_LOADING,
				      EN8811H_FW_CTRL_2_LOADING);
	if (ret < 0)
		goto en8811h_load_firmware_out;

	ret = air_write_buf(phydev, AIR_FW_ADDR_DM,  fw1);
	if (ret < 0)
		goto en8811h_load_firmware_out;

	ret = air_write_buf(phydev, AIR_FW_ADDR_DSP, fw2);
	if (ret < 0)
		goto en8811h_load_firmware_out;

	ret = air_buckpbus_reg_modify(phydev, EN8811H_FW_CTRL_2,
				      EN8811H_FW_CTRL_2_LOADING, 0);
	if (ret < 0)
		goto en8811h_load_firmware_out;

	ret = air_buckpbus_reg_write(phydev, EN8811H_FW_CTRL_1,
				     EN8811H_FW_CTRL_1_FINISH);
	if (ret < 0)
		goto en8811h_load_firmware_out;

	ret = en8811h_wait_mcu_ready(phydev);

	air_buckpbus_reg_read(phydev, EN8811H_FW_VERSION,
			      &priv->firmware_version);
	phydev_info(phydev, "MD32 firmware version: %08x\n",
		    priv->firmware_version);

en8811h_load_firmware_out:
	release_firmware(fw2);

en8811h_load_firmware_rel1:
	release_firmware(fw1);

	if (ret < 0)
		phydev_err(phydev, "Load firmware failed: %d\n", ret);

	return ret;
}

static int en8811h_restart_mcu(struct phy_device *phydev)
{
	int ret;

	ret = air_buckpbus_reg_write(phydev, EN8811H_FW_CTRL_1,
				     EN8811H_FW_CTRL_1_START);
	if (ret < 0)
		return ret;

	ret = air_buckpbus_reg_write(phydev, EN8811H_FW_CTRL_1,
				     EN8811H_FW_CTRL_1_FINISH);
	if (ret < 0)
		return ret;

	return en8811h_wait_mcu_ready(phydev);
}

static int air_hw_led_on_set(struct phy_device *phydev, u8 index, bool on)
{
	struct en8811h_priv *priv = phydev->priv;
	bool changed;

	if (index >= EN8811H_LED_COUNT)
		return -EINVAL;

	if (on)
		changed = !test_and_set_bit(AIR_PHY_LED_STATE_FORCE_ON,
					    &priv->led[index].state);
	else
		changed = !!test_and_clear_bit(AIR_PHY_LED_STATE_FORCE_ON,
					       &priv->led[index].state);

	changed |= (priv->led[index].rules != 0);

	/* clear netdev trigger rules in case LED_OFF has been set */
	if (!on)
		priv->led[index].rules = 0;

	if (changed)
		return phy_modify_mmd(phydev, MDIO_MMD_VEND2,
				      AIR_PHY_LED_ON(index),
				      AIR_PHY_LED_ON_MASK,
				      on ? AIR_PHY_LED_ON_FORCE_ON : 0);

	return 0;
}

static int air_hw_led_blink_set(struct phy_device *phydev, u8 index,
				bool blinking)
{
	struct en8811h_priv *priv = phydev->priv;
	bool changed;

	if (index >= EN8811H_LED_COUNT)
		return -EINVAL;

	if (blinking)
		changed = !test_and_set_bit(AIR_PHY_LED_STATE_FORCE_BLINK,
					    &priv->led[index].state);
	else
		changed = !!test_and_clear_bit(AIR_PHY_LED_STATE_FORCE_BLINK,
					       &priv->led[index].state);

	changed |= (priv->led[index].rules != 0);

	if (changed)
		return phy_write_mmd(phydev, MDIO_MMD_VEND2,
				     AIR_PHY_LED_BLINK(index),
				     blinking ?
				     AIR_PHY_LED_BLINK_FORCE_BLINK : 0);
	else
		return 0;
}

static int air_led_blink_set(struct phy_device *phydev, u8 index,
			     unsigned long *delay_on,
			     unsigned long *delay_off)
{
	struct en8811h_priv *priv = phydev->priv;
	bool blinking = false;
	int err;

	if (index >= EN8811H_LED_COUNT)
		return -EINVAL;

	if (delay_on && delay_off && (*delay_on > 0) && (*delay_off > 0)) {
		blinking = true;
		*delay_on = 50;
		*delay_off = 50;
	}

	err = air_hw_led_blink_set(phydev, index, blinking);
	if (err)
		return err;

	/* led-blink set, so switch led-on off */
	err = air_hw_led_on_set(phydev, index, false);
	if (err)
		return err;

	/* hw-control is off*/
	if (!!test_bit(AIR_PHY_LED_STATE_FORCE_BLINK, &priv->led[index].state))
		priv->led[index].rules = 0;

	return 0;
}

static int air_led_brightness_set(struct phy_device *phydev, u8 index,
				  enum led_brightness value)
{
	struct en8811h_priv *priv = phydev->priv;
	int err;

	if (index >= EN8811H_LED_COUNT)
		return -EINVAL;

	/* led-on set, so switch led-blink off */
	err = air_hw_led_blink_set(phydev, index, false);
	if (err)
		return err;

	err = air_hw_led_on_set(phydev, index, (value != LED_OFF));
	if (err)
		return err;

	/* hw-control is off */
	if (!!test_bit(AIR_PHY_LED_STATE_FORCE_ON, &priv->led[index].state))
		priv->led[index].rules = 0;

	return 0;
}

static int air_led_hw_control_get(struct phy_device *phydev, u8 index,
				  unsigned long *rules)
{
	struct en8811h_priv *priv = phydev->priv;

	if (index >= EN8811H_LED_COUNT)
		return -EINVAL;

	*rules = priv->led[index].rules;

	return 0;
};

static int air_led_hw_control_set(struct phy_device *phydev, u8 index,
				  unsigned long rules)
{
	struct en8811h_priv *priv = phydev->priv;
	u16 on = 0, blink = 0;
	int ret;

	if (index >= EN8811H_LED_COUNT)
		return -EINVAL;

	priv->led[index].rules = rules;

	if (rules & BIT(TRIGGER_NETDEV_FULL_DUPLEX))
		on |= AIR_PHY_LED_ON_FDX;

	if (rules & (BIT(TRIGGER_NETDEV_LINK_10) | BIT(TRIGGER_NETDEV_LINK)))
		on |= AIR_PHY_LED_ON_LINK10;

	if (rules & (BIT(TRIGGER_NETDEV_LINK_100) | BIT(TRIGGER_NETDEV_LINK)))
		on |= AIR_PHY_LED_ON_LINK100;

	if (rules & (BIT(TRIGGER_NETDEV_LINK_1000) | BIT(TRIGGER_NETDEV_LINK)))
		on |= AIR_PHY_LED_ON_LINK1000;

	if (rules & (BIT(TRIGGER_NETDEV_LINK_2500) | BIT(TRIGGER_NETDEV_LINK)))
		on |= AIR_PHY_LED_ON_LINK2500;

	if (rules & BIT(TRIGGER_NETDEV_RX)) {
		blink |= AIR_PHY_LED_BLINK_10RX   |
			 AIR_PHY_LED_BLINK_100RX  |
			 AIR_PHY_LED_BLINK_1000RX |
			 AIR_PHY_LED_BLINK_2500RX;
	}

	if (rules & BIT(TRIGGER_NETDEV_TX)) {
		blink |= AIR_PHY_LED_BLINK_10TX   |
			 AIR_PHY_LED_BLINK_100TX  |
			 AIR_PHY_LED_BLINK_1000TX |
			 AIR_PHY_LED_BLINK_2500TX;
	}

	if (blink || on) {
		/* switch hw-control on, so led-on and led-blink are off */
		clear_bit(AIR_PHY_LED_STATE_FORCE_ON,
			  &priv->led[index].state);
		clear_bit(AIR_PHY_LED_STATE_FORCE_BLINK,
			  &priv->led[index].state);
	} else {
		priv->led[index].rules = 0;
	}

	ret = phy_modify_mmd(phydev, MDIO_MMD_VEND2, AIR_PHY_LED_ON(index),
			     AIR_PHY_LED_ON_MASK, on);

	if (ret < 0)
		return ret;

	return phy_write_mmd(phydev, MDIO_MMD_VEND2, AIR_PHY_LED_BLINK(index),
			     blink);
};

static int air_led_init(struct phy_device *phydev, u8 index, u8 state, u8 pol)
{
	int val = 0;
	int err;

	if (index >= EN8811H_LED_COUNT)
		return -EINVAL;

	if (state == AIR_LED_ENABLE)
		val |= AIR_PHY_LED_ON_ENABLE;
	else
		val &= ~AIR_PHY_LED_ON_ENABLE;

	if (pol == AIR_ACTIVE_HIGH)
		val |= AIR_PHY_LED_ON_POLARITY;
	else
		val &= ~AIR_PHY_LED_ON_POLARITY;

	err = phy_modify_mmd(phydev, MDIO_MMD_VEND2, AIR_PHY_LED_ON(index),
			     AIR_PHY_LED_ON_ENABLE |
			     AIR_PHY_LED_ON_POLARITY, val);

	if (err < 0)
		return err;

	return 0;
}

static int air_leds_init(struct phy_device *phydev, int num, int dur, int mode)
{
	struct en8811h_priv *priv = phydev->priv;
	int ret, i;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, AIR_PHY_LED_DUR_BLINK,
			    dur);
	if (ret < 0)
		return ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, AIR_PHY_LED_DUR_ON,
			    dur >> 1);
	if (ret < 0)
		return ret;

	switch (mode) {
	case AIR_LED_MODE_DISABLE:
		ret = phy_modify_mmd(phydev, MDIO_MMD_VEND2, AIR_PHY_LED_BCR,
				     AIR_PHY_LED_BCR_EXT_CTRL |
				     AIR_PHY_LED_BCR_MODE_MASK, 0);
		if (ret < 0)
			return ret;
		break;
	case AIR_LED_MODE_USER_DEFINE:
		ret = phy_modify_mmd(phydev, MDIO_MMD_VEND2, AIR_PHY_LED_BCR,
				     AIR_PHY_LED_BCR_EXT_CTRL |
				     AIR_PHY_LED_BCR_CLK_EN,
				     AIR_PHY_LED_BCR_EXT_CTRL |
				     AIR_PHY_LED_BCR_CLK_EN);
		if (ret < 0)
			return ret;
		break;
	default:
		phydev_err(phydev, "LED mode %d is not supported\n", mode);
		return -EINVAL;
	}

	for (i = 0; i < num; ++i) {
		ret = air_led_init(phydev, i, AIR_LED_ENABLE, AIR_ACTIVE_HIGH);
		if (ret < 0) {
			phydev_err(phydev, "LED%d init failed: %d\n", i, ret);
			return ret;
		}
		air_led_hw_control_set(phydev, i, priv->led[i].rules);
	}

	return 0;
}

static int en8811h_led_hw_is_supported(struct phy_device *phydev, u8 index,
				       unsigned long rules)
{
	if (index >= EN8811H_LED_COUNT)
		return -EINVAL;

	/* All combinations of the supported triggers are allowed */
	if (rules & ~en8811h_led_trig)
		return -EOPNOTSUPP;

	return 0;
};

static unsigned long en8811h_clk_recalc_rate(struct clk_hw *hw,
					     unsigned long parent)
{
	struct en8811h_priv *priv = clk_hw_to_en8811h_priv(hw);
	struct phy_device *phydev = priv->phydev;
	u32 pbus_value;
	int ret;

	ret = air_buckpbus_reg_read(phydev, EN8811H_HWTRAP1, &pbus_value);
	if (ret < 0)
		return ret;

	return (pbus_value & EN8811H_HWTRAP1_CKO) ? 50000000 : 25000000;
}

static int en8811h_clk_enable(struct clk_hw *hw)
{
	struct en8811h_priv *priv = clk_hw_to_en8811h_priv(hw);
	struct phy_device *phydev = priv->phydev;

	return air_buckpbus_reg_modify(phydev, EN8811H_CLK_CGM,
				       EN8811H_CLK_CGM_CKO,
				       EN8811H_CLK_CGM_CKO);
}

static void en8811h_clk_disable(struct clk_hw *hw)
{
	struct en8811h_priv *priv = clk_hw_to_en8811h_priv(hw);
	struct phy_device *phydev = priv->phydev;

	air_buckpbus_reg_modify(phydev, EN8811H_CLK_CGM,
				EN8811H_CLK_CGM_CKO, 0);
}

static int en8811h_clk_is_enabled(struct clk_hw *hw)
{
	struct en8811h_priv *priv = clk_hw_to_en8811h_priv(hw);
	struct phy_device *phydev = priv->phydev;
	u32 pbus_value;
	int ret;

	ret = air_buckpbus_reg_read(phydev, EN8811H_CLK_CGM, &pbus_value);
	if (ret < 0)
		return ret;

	return (pbus_value & EN8811H_CLK_CGM_CKO);
}

static int en8811h_clk_save_context(struct clk_hw *hw)
{
	struct en8811h_priv *priv = clk_hw_to_en8811h_priv(hw);

	priv->cko_is_enabled = en8811h_clk_is_enabled(hw);

	return 0;
}

static void en8811h_clk_restore_context(struct clk_hw *hw)
{
	struct en8811h_priv *priv = clk_hw_to_en8811h_priv(hw);

	if (!priv->cko_is_enabled)
		en8811h_clk_disable(hw);
}

static const struct clk_ops en8811h_clk_ops = {
	.recalc_rate		= en8811h_clk_recalc_rate,
	.enable			= en8811h_clk_enable,
	.disable		= en8811h_clk_disable,
	.is_enabled		= en8811h_clk_is_enabled,
	.save_context		= en8811h_clk_save_context,
	.restore_context	= en8811h_clk_restore_context,
};

static int en8811h_clk_provider_setup(struct device *dev, struct clk_hw *hw)
{
	struct clk_init_data init;
	int ret;

	if (!IS_ENABLED(CONFIG_COMMON_CLK))
		return 0;

	init.name = devm_kasprintf(dev, GFP_KERNEL, "%s-cko",
				   fwnode_get_name(dev_fwnode(dev)));
	if (!init.name)
		return -ENOMEM;

	init.ops = &en8811h_clk_ops;
	init.flags = 0;
	init.num_parents = 0;
	hw->init = &init;

	ret = devm_clk_hw_register(dev, hw);
	if (ret)
		return ret;

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get, hw);
}

static int en8811h_probe(struct phy_device *phydev)
{
	struct en8811h_priv *priv;
	int ret;

	priv = devm_kzalloc(&phydev->mdio.dev, sizeof(struct en8811h_priv),
			    GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	phydev->priv = priv;

	ret = en8811h_load_firmware(phydev);
	if (ret < 0)
		return ret;

	/* mcu has just restarted after firmware load */
	priv->mcu_needs_restart = false;

	priv->led[0].rules = AIR_DEFAULT_TRIGGER_LED0;
	priv->led[1].rules = AIR_DEFAULT_TRIGGER_LED1;
	priv->led[2].rules = AIR_DEFAULT_TRIGGER_LED2;

	/* MDIO_DEVS1/2 empty, so set mmds_present bits here */
	phydev->c45_ids.mmds_present |= MDIO_DEVS_PMAPMD | MDIO_DEVS_AN;

	ret = air_leds_init(phydev, EN8811H_LED_COUNT, AIR_PHY_LED_DUR,
			    AIR_LED_MODE_DISABLE);
	if (ret < 0) {
		phydev_err(phydev, "Failed to disable leds: %d\n", ret);
		return ret;
	}

	priv->phydev = phydev;
	/* Co-Clock Output */
	ret = en8811h_clk_provider_setup(&phydev->mdio.dev, &priv->hw);
	if (ret)
		return ret;

	/* Configure led gpio pins as output */
	ret = air_buckpbus_reg_modify(phydev, EN8811H_GPIO_OUTPUT,
				      EN8811H_GPIO_OUTPUT_345,
				      EN8811H_GPIO_OUTPUT_345);
	if (ret < 0)
		return ret;

	return 0;
}

static int en8811h_config_init(struct phy_device *phydev)
{
	struct en8811h_priv *priv = phydev->priv;
	struct device *dev = &phydev->mdio.dev;
	u32 pbus_value;
	int ret;

	/* If restart happened in .probe(), no need to restart now */
	if (priv->mcu_needs_restart) {
		ret = en8811h_restart_mcu(phydev);
		if (ret < 0)
			return ret;
	} else {
		/* Next calls to .config_init() mcu needs to restart */
		priv->mcu_needs_restart = true;
	}

	/* Select mode 1, the only mode supported.
	 * Configures the SerDes for 2500Base-X with rate adaptation
	 */
	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1, AIR_PHY_MCU_CMD_1,
			    AIR_PHY_MCU_CMD_1_MODE1);
	if (ret < 0)
		return ret;
	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1, AIR_PHY_MCU_CMD_2,
			    AIR_PHY_MCU_CMD_2_MODE1);
	if (ret < 0)
		return ret;
	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1, AIR_PHY_MCU_CMD_3,
			    AIR_PHY_MCU_CMD_3_MODE1);
	if (ret < 0)
		return ret;
	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1, AIR_PHY_MCU_CMD_4,
			    AIR_PHY_MCU_CMD_4_MODE1);
	if (ret < 0)
		return ret;

	/* Serdes polarity */
	pbus_value = 0;
	if (device_property_read_bool(dev, "airoha,pnswap-rx"))
		pbus_value |=  EN8811H_POLARITY_RX_REVERSE;
	else
		pbus_value &= ~EN8811H_POLARITY_RX_REVERSE;
	if (device_property_read_bool(dev, "airoha,pnswap-tx"))
		pbus_value &= ~EN8811H_POLARITY_TX_NORMAL;
	else
		pbus_value |=  EN8811H_POLARITY_TX_NORMAL;
	ret = air_buckpbus_reg_modify(phydev, EN8811H_POLARITY,
				      EN8811H_POLARITY_RX_REVERSE |
				      EN8811H_POLARITY_TX_NORMAL, pbus_value);
	if (ret < 0)
		return ret;

	ret = air_leds_init(phydev, EN8811H_LED_COUNT, AIR_PHY_LED_DUR,
			    AIR_LED_MODE_USER_DEFINE);
	if (ret < 0) {
		phydev_err(phydev, "Failed to initialize leds: %d\n", ret);
		return ret;
	}

	return 0;
}

static int en8811h_get_features(struct phy_device *phydev)
{
	linkmode_set_bit_array(phy_basic_ports_array,
			       ARRAY_SIZE(phy_basic_ports_array),
			       phydev->supported);

	return genphy_c45_pma_read_abilities(phydev);
}

static int en8811h_get_rate_matching(struct phy_device *phydev,
				     phy_interface_t iface)
{
	return RATE_MATCH_PAUSE;
}

static int en8811h_config_aneg(struct phy_device *phydev)
{
	bool changed = false;
	int ret;
	u32 adv;

	if (phydev->autoneg == AUTONEG_DISABLE) {
		phydev_warn(phydev, "Disabling autoneg is not supported\n");
		return -EINVAL;
	}

	adv = linkmode_adv_to_mii_10gbt_adv_t(phydev->advertising);

	ret = phy_modify_mmd_changed(phydev, MDIO_MMD_AN, MDIO_AN_10GBT_CTRL,
				     MDIO_AN_10GBT_CTRL_ADV2_5G, adv);
	if (ret < 0)
		return ret;
	if (ret > 0)
		changed = true;

	return __genphy_config_aneg(phydev, changed);
}

static int en8811h_read_status(struct phy_device *phydev)
{
	struct en8811h_priv *priv = phydev->priv;
	u32 pbus_value;
	int ret, val;

	ret = genphy_update_link(phydev);
	if (ret)
		return ret;

	phydev->master_slave_get = MASTER_SLAVE_CFG_UNSUPPORTED;
	phydev->master_slave_state = MASTER_SLAVE_STATE_UNSUPPORTED;
	phydev->speed = SPEED_UNKNOWN;
	phydev->duplex = DUPLEX_UNKNOWN;
	phydev->pause = 0;
	phydev->asym_pause = 0;
	phydev->rate_matching = RATE_MATCH_PAUSE;

	ret = genphy_read_master_slave(phydev);
	if (ret < 0)
		return ret;

	ret = genphy_read_lpa(phydev);
	if (ret < 0)
		return ret;

	/* Get link partner 2.5GBASE-T ability from vendor register */
	ret = air_buckpbus_reg_read(phydev, EN8811H_2P5G_LPA, &pbus_value);
	if (ret < 0)
		return ret;
	linkmode_mod_bit(ETHTOOL_LINK_MODE_2500baseT_Full_BIT,
			 phydev->lp_advertising,
			 pbus_value & EN8811H_2P5G_LPA_2P5G);

	if (phydev->autoneg_complete)
		phy_resolve_aneg_pause(phydev);

	if (!phydev->link)
		return 0;

	/* Get real speed from vendor register */
	val = phy_read(phydev, AIR_AUX_CTRL_STATUS);
	if (val < 0)
		return val;
	switch (val & AIR_AUX_CTRL_STATUS_SPEED_MASK) {
	case AIR_AUX_CTRL_STATUS_SPEED_2500:
		phydev->speed = SPEED_2500;
		break;
	case AIR_AUX_CTRL_STATUS_SPEED_1000:
		phydev->speed = SPEED_1000;
		break;
	case AIR_AUX_CTRL_STATUS_SPEED_100:
		phydev->speed = SPEED_100;
		break;
	}

	/* Firmware before version 24011202 has no vendor register 2P5G_LPA.
	 * Assume link partner advertised it if connected at 2500Mbps.
	 */
	if (priv->firmware_version < 0x24011202) {
		linkmode_mod_bit(ETHTOOL_LINK_MODE_2500baseT_Full_BIT,
				 phydev->lp_advertising,
				 phydev->speed == SPEED_2500);
	}

	/* Only supports full duplex */
	phydev->duplex = DUPLEX_FULL;

	return 0;
}

static int en8811h_clear_intr(struct phy_device *phydev)
{
	int ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1, AIR_PHY_MCU_CMD_3,
			    AIR_PHY_MCU_CMD_3_DOCMD);
	if (ret < 0)
		return ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1, AIR_PHY_MCU_CMD_4,
			    AIR_PHY_MCU_CMD_4_INTCLR);
	if (ret < 0)
		return ret;

	return 0;
}

static irqreturn_t en8811h_handle_interrupt(struct phy_device *phydev)
{
	int ret;

	ret = en8811h_clear_intr(phydev);
	if (ret < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	phy_trigger_machine(phydev);

	return IRQ_HANDLED;
}

static int en8811h_resume(struct phy_device *phydev)
{
	clk_restore_context();

	return genphy_resume(phydev);
}

static int en8811h_suspend(struct phy_device *phydev)
{
	clk_save_context();

	return genphy_suspend(phydev);
}

static struct phy_driver en8811h_driver[] = {
{
	PHY_ID_MATCH_MODEL(EN8811H_PHY_ID),
	.name			= "Airoha EN8811H",
	.probe			= en8811h_probe,
	.get_features		= en8811h_get_features,
	.config_init		= en8811h_config_init,
	.get_rate_matching	= en8811h_get_rate_matching,
	.config_aneg		= en8811h_config_aneg,
	.read_status		= en8811h_read_status,
	.resume			= en8811h_resume,
	.suspend		= en8811h_suspend,
	.config_intr		= en8811h_clear_intr,
	.handle_interrupt	= en8811h_handle_interrupt,
	.led_hw_is_supported	= en8811h_led_hw_is_supported,
	.read_page		= air_phy_read_page,
	.write_page		= air_phy_write_page,
	.led_blink_set		= air_led_blink_set,
	.led_brightness_set	= air_led_brightness_set,
	.led_hw_control_set	= air_led_hw_control_set,
	.led_hw_control_get	= air_led_hw_control_get,
} };

module_phy_driver(en8811h_driver);

static const struct mdio_device_id __maybe_unused en8811h_tbl[] = {
	{ PHY_ID_MATCH_MODEL(EN8811H_PHY_ID) },
	{ }
};

MODULE_DEVICE_TABLE(mdio, en8811h_tbl);
MODULE_FIRMWARE(EN8811H_MD32_DM);
MODULE_FIRMWARE(EN8811H_MD32_DSP);

MODULE_DESCRIPTION("Airoha EN8811H PHY drivers");
MODULE_AUTHOR("Airoha");
MODULE_AUTHOR("Eric Woudstra <ericwouds@gmail.com>");
MODULE_LICENSE("GPL");
