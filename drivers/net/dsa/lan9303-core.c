/*
 * Copyright (C) 2017 Pengutronix, Juergen Borleis <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gpio/consumer.h>
#include <linux/regmap.h>
#include <linux/mutex.h>
#include <linux/mii.h>

#include "lan9303.h"

#define LAN9303_CHIP_REV 0x14
# define LAN9303_CHIP_ID 0x9303
#define LAN9303_IRQ_CFG 0x15
# define LAN9303_IRQ_CFG_IRQ_ENABLE BIT(8)
# define LAN9303_IRQ_CFG_IRQ_POL BIT(4)
# define LAN9303_IRQ_CFG_IRQ_TYPE BIT(0)
#define LAN9303_INT_STS 0x16
# define LAN9303_INT_STS_PHY_INT2 BIT(27)
# define LAN9303_INT_STS_PHY_INT1 BIT(26)
#define LAN9303_INT_EN 0x17
# define LAN9303_INT_EN_PHY_INT2_EN BIT(27)
# define LAN9303_INT_EN_PHY_INT1_EN BIT(26)
#define LAN9303_HW_CFG 0x1D
# define LAN9303_HW_CFG_READY BIT(27)
# define LAN9303_HW_CFG_AMDX_EN_PORT2 BIT(26)
# define LAN9303_HW_CFG_AMDX_EN_PORT1 BIT(25)
#define LAN9303_PMI_DATA 0x29
#define LAN9303_PMI_ACCESS 0x2A
# define LAN9303_PMI_ACCESS_PHY_ADDR(x) (((x) & 0x1f) << 11)
# define LAN9303_PMI_ACCESS_MIIRINDA(x) (((x) & 0x1f) << 6)
# define LAN9303_PMI_ACCESS_MII_BUSY BIT(0)
# define LAN9303_PMI_ACCESS_MII_WRITE BIT(1)
#define LAN9303_MANUAL_FC_1 0x68
#define LAN9303_MANUAL_FC_2 0x69
#define LAN9303_MANUAL_FC_0 0x6a
#define LAN9303_SWITCH_CSR_DATA 0x6b
#define LAN9303_SWITCH_CSR_CMD 0x6c
#define LAN9303_SWITCH_CSR_CMD_BUSY BIT(31)
#define LAN9303_SWITCH_CSR_CMD_RW BIT(30)
#define LAN9303_SWITCH_CSR_CMD_LANES (BIT(19) | BIT(18) | BIT(17) | BIT(16))
#define LAN9303_VIRT_PHY_BASE 0x70
#define LAN9303_VIRT_SPECIAL_CTRL 0x77

#define LAN9303_SW_DEV_ID 0x0000
#define LAN9303_SW_RESET 0x0001
#define LAN9303_SW_RESET_RESET BIT(0)
#define LAN9303_SW_IMR 0x0004
#define LAN9303_SW_IPR 0x0005
#define LAN9303_MAC_VER_ID_0 0x0400
#define LAN9303_MAC_RX_CFG_0 0x0401
# define LAN9303_MAC_RX_CFG_X_REJECT_MAC_TYPES BIT(1)
# define LAN9303_MAC_RX_CFG_X_RX_ENABLE BIT(0)
#define LAN9303_MAC_RX_UNDSZE_CNT_0 0x0410
#define LAN9303_MAC_RX_64_CNT_0 0x0411
#define LAN9303_MAC_RX_127_CNT_0 0x0412
#define LAN9303_MAC_RX_255_CNT_0 0x413
#define LAN9303_MAC_RX_511_CNT_0 0x0414
#define LAN9303_MAC_RX_1023_CNT_0 0x0415
#define LAN9303_MAC_RX_MAX_CNT_0 0x0416
#define LAN9303_MAC_RX_OVRSZE_CNT_0 0x0417
#define LAN9303_MAC_RX_PKTOK_CNT_0 0x0418
#define LAN9303_MAC_RX_CRCERR_CNT_0 0x0419
#define LAN9303_MAC_RX_MULCST_CNT_0 0x041a
#define LAN9303_MAC_RX_BRDCST_CNT_0 0x041b
#define LAN9303_MAC_RX_PAUSE_CNT_0 0x041c
#define LAN9303_MAC_RX_FRAG_CNT_0 0x041d
#define LAN9303_MAC_RX_JABB_CNT_0 0x041e
#define LAN9303_MAC_RX_ALIGN_CNT_0 0x041f
#define LAN9303_MAC_RX_PKTLEN_CNT_0 0x0420
#define LAN9303_MAC_RX_GOODPKTLEN_CNT_0 0x0421
#define LAN9303_MAC_RX_SYMBL_CNT_0 0x0422
#define LAN9303_MAC_RX_CTLFRM_CNT_0 0x0423

#define LAN9303_MAC_TX_CFG_0 0x0440
# define LAN9303_MAC_TX_CFG_X_TX_IFG_CONFIG_DEFAULT (21 << 2)
# define LAN9303_MAC_TX_CFG_X_TX_PAD_ENABLE BIT(1)
# define LAN9303_MAC_TX_CFG_X_TX_ENABLE BIT(0)
#define LAN9303_MAC_TX_DEFER_CNT_0 0x0451
#define LAN9303_MAC_TX_PAUSE_CNT_0 0x0452
#define LAN9303_MAC_TX_PKTOK_CNT_0 0x0453
#define LAN9303_MAC_TX_64_CNT_0 0x0454
#define LAN9303_MAC_TX_127_CNT_0 0x0455
#define LAN9303_MAC_TX_255_CNT_0 0x0456
#define LAN9303_MAC_TX_511_CNT_0 0x0457
#define LAN9303_MAC_TX_1023_CNT_0 0x0458
#define LAN9303_MAC_TX_MAX_CNT_0 0x0459
#define LAN9303_MAC_TX_UNDSZE_CNT_0 0x045a
#define LAN9303_MAC_TX_PKTLEN_CNT_0 0x045c
#define LAN9303_MAC_TX_BRDCST_CNT_0 0x045d
#define LAN9303_MAC_TX_MULCST_CNT_0 0x045e
#define LAN9303_MAC_TX_LATECOL_0 0x045f
#define LAN9303_MAC_TX_EXCOL_CNT_0 0x0460
#define LAN9303_MAC_TX_SNGLECOL_CNT_0 0x0461
#define LAN9303_MAC_TX_MULTICOL_CNT_0 0x0462
#define LAN9303_MAC_TX_TOTALCOL_CNT_0 0x0463

#define LAN9303_MAC_VER_ID_1 0x0800
#define LAN9303_MAC_RX_CFG_1 0x0801
#define LAN9303_MAC_TX_CFG_1 0x0840
#define LAN9303_MAC_VER_ID_2 0x0c00
#define LAN9303_MAC_RX_CFG_2 0x0c01
#define LAN9303_MAC_TX_CFG_2 0x0c40
#define LAN9303_SWE_ALR_CMD 0x1800
#define LAN9303_SWE_VLAN_CMD 0x180b
# define LAN9303_SWE_VLAN_CMD_RNW BIT(5)
# define LAN9303_SWE_VLAN_CMD_PVIDNVLAN BIT(4)
#define LAN9303_SWE_VLAN_WR_DATA 0x180c
#define LAN9303_SWE_VLAN_RD_DATA 0x180e
# define LAN9303_SWE_VLAN_MEMBER_PORT2 BIT(17)
# define LAN9303_SWE_VLAN_UNTAG_PORT2 BIT(16)
# define LAN9303_SWE_VLAN_MEMBER_PORT1 BIT(15)
# define LAN9303_SWE_VLAN_UNTAG_PORT1 BIT(14)
# define LAN9303_SWE_VLAN_MEMBER_PORT0 BIT(13)
# define LAN9303_SWE_VLAN_UNTAG_PORT0 BIT(12)
#define LAN9303_SWE_VLAN_CMD_STS 0x1810
#define LAN9303_SWE_GLB_INGRESS_CFG 0x1840
#define LAN9303_SWE_PORT_STATE 0x1843
# define LAN9303_SWE_PORT_STATE_FORWARDING_PORT2 (0)
# define LAN9303_SWE_PORT_STATE_LEARNING_PORT2 BIT(5)
# define LAN9303_SWE_PORT_STATE_BLOCKING_PORT2 BIT(4)
# define LAN9303_SWE_PORT_STATE_FORWARDING_PORT1 (0)
# define LAN9303_SWE_PORT_STATE_LEARNING_PORT1 BIT(3)
# define LAN9303_SWE_PORT_STATE_BLOCKING_PORT1 BIT(2)
# define LAN9303_SWE_PORT_STATE_FORWARDING_PORT0 (0)
# define LAN9303_SWE_PORT_STATE_LEARNING_PORT0 BIT(1)
# define LAN9303_SWE_PORT_STATE_BLOCKING_PORT0 BIT(0)
#define LAN9303_SWE_PORT_MIRROR 0x1846
# define LAN9303_SWE_PORT_MIRROR_SNIFF_ALL BIT(8)
# define LAN9303_SWE_PORT_MIRROR_SNIFFER_PORT2 BIT(7)
# define LAN9303_SWE_PORT_MIRROR_SNIFFER_PORT1 BIT(6)
# define LAN9303_SWE_PORT_MIRROR_SNIFFER_PORT0 BIT(5)
# define LAN9303_SWE_PORT_MIRROR_MIRRORED_PORT2 BIT(4)
# define LAN9303_SWE_PORT_MIRROR_MIRRORED_PORT1 BIT(3)
# define LAN9303_SWE_PORT_MIRROR_MIRRORED_PORT0 BIT(2)
# define LAN9303_SWE_PORT_MIRROR_ENABLE_RX_MIRRORING BIT(1)
# define LAN9303_SWE_PORT_MIRROR_ENABLE_TX_MIRRORING BIT(0)
#define LAN9303_SWE_INGRESS_PORT_TYPE 0x1847
#define LAN9303_BM_CFG 0x1c00
#define LAN9303_BM_EGRSS_PORT_TYPE 0x1c0c
# define LAN9303_BM_EGRSS_PORT_TYPE_SPECIAL_TAG_PORT2 (BIT(17) | BIT(16))
# define LAN9303_BM_EGRSS_PORT_TYPE_SPECIAL_TAG_PORT1 (BIT(9) | BIT(8))
# define LAN9303_BM_EGRSS_PORT_TYPE_SPECIAL_TAG_PORT0 (BIT(1) | BIT(0))

#define LAN9303_PORT_0_OFFSET 0x400
#define LAN9303_PORT_1_OFFSET 0x800
#define LAN9303_PORT_2_OFFSET 0xc00

/* the built-in PHYs are of type LAN911X */
#define MII_LAN911X_SPECIAL_MODES 0x12
#define MII_LAN911X_SPECIAL_CONTROL_STATUS 0x1f

static const struct regmap_range lan9303_valid_regs[] = {
	regmap_reg_range(0x14, 0x17), /* misc, interrupt */
	regmap_reg_range(0x19, 0x19), /* endian test */
	regmap_reg_range(0x1d, 0x1d), /* hardware config */
	regmap_reg_range(0x23, 0x24), /* general purpose timer */
	regmap_reg_range(0x27, 0x27), /* counter */
	regmap_reg_range(0x29, 0x2a), /* PMI index regs */
	regmap_reg_range(0x68, 0x6a), /* flow control */
	regmap_reg_range(0x6b, 0x6c), /* switch fabric indirect regs */
	regmap_reg_range(0x6d, 0x6f), /* misc */
	regmap_reg_range(0x70, 0x77), /* virtual phy */
	regmap_reg_range(0x78, 0x7a), /* GPIO */
	regmap_reg_range(0x7c, 0x7e), /* MAC & reset */
	regmap_reg_range(0x80, 0xb7), /* switch fabric direct regs (wr only) */
};

static const struct regmap_range lan9303_reserved_ranges[] = {
	regmap_reg_range(0x00, 0x13),
	regmap_reg_range(0x18, 0x18),
	regmap_reg_range(0x1a, 0x1c),
	regmap_reg_range(0x1e, 0x22),
	regmap_reg_range(0x25, 0x26),
	regmap_reg_range(0x28, 0x28),
	regmap_reg_range(0x2b, 0x67),
	regmap_reg_range(0x7b, 0x7b),
	regmap_reg_range(0x7f, 0x7f),
	regmap_reg_range(0xb8, 0xff),
};

const struct regmap_access_table lan9303_register_set = {
	.yes_ranges = lan9303_valid_regs,
	.n_yes_ranges = ARRAY_SIZE(lan9303_valid_regs),
	.no_ranges = lan9303_reserved_ranges,
	.n_no_ranges = ARRAY_SIZE(lan9303_reserved_ranges),
};
EXPORT_SYMBOL(lan9303_register_set);

static int lan9303_read(struct regmap *regmap, unsigned int offset, u32 *reg)
{
	int ret, i;

	/* we can lose arbitration for the I2C case, because the device
	 * tries to detect and read an external EEPROM after reset and acts as
	 * a master on the shared I2C bus itself. This conflicts with our
	 * attempts to access the device as a slave at the same moment.
	 */
	for (i = 0; i < 5; i++) {
		ret = regmap_read(regmap, offset, reg);
		if (!ret)
			return 0;
		if (ret != -EAGAIN)
			break;
		msleep(500);
	}

	return -EIO;
}

static int lan9303_virt_phy_reg_read(struct lan9303 *chip, int regnum)
{
	int ret;
	u32 val;

	if (regnum > MII_EXPANSION)
		return -EINVAL;

	ret = lan9303_read(chip->regmap, LAN9303_VIRT_PHY_BASE + regnum, &val);
	if (ret)
		return ret;

	return val & 0xffff;
}

static int lan9303_virt_phy_reg_write(struct lan9303 *chip, int regnum, u16 val)
{
	if (regnum > MII_EXPANSION)
		return -EINVAL;

	return regmap_write(chip->regmap, LAN9303_VIRT_PHY_BASE + regnum, val);
}

static int lan9303_port_phy_reg_wait_for_completion(struct lan9303 *chip)
{
	int ret, i;
	u32 reg;

	for (i = 0; i < 25; i++) {
		ret = lan9303_read(chip->regmap, LAN9303_PMI_ACCESS, &reg);
		if (ret) {
			dev_err(chip->dev,
				"Failed to read pmi access status: %d\n", ret);
			return ret;
		}
		if (!(reg & LAN9303_PMI_ACCESS_MII_BUSY))
			return 0;
		msleep(1);
	}

	return -EIO;
}

static int lan9303_port_phy_reg_read(struct lan9303 *chip, int addr, int regnum)
{
	int ret;
	u32 val;

	val = LAN9303_PMI_ACCESS_PHY_ADDR(addr);
	val |= LAN9303_PMI_ACCESS_MIIRINDA(regnum);

	mutex_lock(&chip->indirect_mutex);

	ret = lan9303_port_phy_reg_wait_for_completion(chip);
	if (ret)
		goto on_error;

	/* start the MII read cycle */
	ret = regmap_write(chip->regmap, LAN9303_PMI_ACCESS, val);
	if (ret)
		goto on_error;

	ret = lan9303_port_phy_reg_wait_for_completion(chip);
	if (ret)
		goto on_error;

	/* read the result of this operation */
	ret = lan9303_read(chip->regmap, LAN9303_PMI_DATA, &val);
	if (ret)
		goto on_error;

	mutex_unlock(&chip->indirect_mutex);

	return val & 0xffff;

on_error:
	mutex_unlock(&chip->indirect_mutex);
	return ret;
}

static int lan9303_phy_reg_write(struct lan9303 *chip, int addr, int regnum,
				 unsigned int val)
{
	int ret;
	u32 reg;

	reg = LAN9303_PMI_ACCESS_PHY_ADDR(addr);
	reg |= LAN9303_PMI_ACCESS_MIIRINDA(regnum);
	reg |= LAN9303_PMI_ACCESS_MII_WRITE;

	mutex_lock(&chip->indirect_mutex);

	ret = lan9303_port_phy_reg_wait_for_completion(chip);
	if (ret)
		goto on_error;

	/* write the data first... */
	ret = regmap_write(chip->regmap, LAN9303_PMI_DATA, val);
	if (ret)
		goto on_error;

	/* ...then start the MII write cycle */
	ret = regmap_write(chip->regmap, LAN9303_PMI_ACCESS, reg);

on_error:
	mutex_unlock(&chip->indirect_mutex);
	return ret;
}

static int lan9303_switch_wait_for_completion(struct lan9303 *chip)
{
	int ret, i;
	u32 reg;

	for (i = 0; i < 25; i++) {
		ret = lan9303_read(chip->regmap, LAN9303_SWITCH_CSR_CMD, &reg);
		if (ret) {
			dev_err(chip->dev,
				"Failed to read csr command status: %d\n", ret);
			return ret;
		}
		if (!(reg & LAN9303_SWITCH_CSR_CMD_BUSY))
			return 0;
		msleep(1);
	}

	return -EIO;
}

static int lan9303_write_switch_reg(struct lan9303 *chip, u16 regnum, u32 val)
{
	u32 reg;
	int ret;

	reg = regnum;
	reg |= LAN9303_SWITCH_CSR_CMD_LANES;
	reg |= LAN9303_SWITCH_CSR_CMD_BUSY;

	mutex_lock(&chip->indirect_mutex);

	ret = lan9303_switch_wait_for_completion(chip);
	if (ret)
		goto on_error;

	ret = regmap_write(chip->regmap, LAN9303_SWITCH_CSR_DATA, val);
	if (ret) {
		dev_err(chip->dev, "Failed to write csr data reg: %d\n", ret);
		goto on_error;
	}

	/* trigger write */
	ret = regmap_write(chip->regmap, LAN9303_SWITCH_CSR_CMD, reg);
	if (ret)
		dev_err(chip->dev, "Failed to write csr command reg: %d\n",
			ret);

on_error:
	mutex_unlock(&chip->indirect_mutex);
	return ret;
}

static int lan9303_read_switch_reg(struct lan9303 *chip, u16 regnum, u32 *val)
{
	u32 reg;
	int ret;

	reg = regnum;
	reg |= LAN9303_SWITCH_CSR_CMD_LANES;
	reg |= LAN9303_SWITCH_CSR_CMD_RW;
	reg |= LAN9303_SWITCH_CSR_CMD_BUSY;

	mutex_lock(&chip->indirect_mutex);

	ret = lan9303_switch_wait_for_completion(chip);
	if (ret)
		goto on_error;

	/* trigger read */
	ret = regmap_write(chip->regmap, LAN9303_SWITCH_CSR_CMD, reg);
	if (ret) {
		dev_err(chip->dev, "Failed to write csr command reg: %d\n",
			ret);
		goto on_error;
	}

	ret = lan9303_switch_wait_for_completion(chip);
	if (ret)
		goto on_error;

	ret = lan9303_read(chip->regmap, LAN9303_SWITCH_CSR_DATA, val);
	if (ret)
		dev_err(chip->dev, "Failed to read csr data reg: %d\n", ret);
on_error:
	mutex_unlock(&chip->indirect_mutex);
	return ret;
}

static int lan9303_detect_phy_setup(struct lan9303 *chip)
{
	int reg;

	/* depending on the 'phy_addr_sel_strap' setting, the three phys are
	 * using IDs 0-1-2 or IDs 1-2-3. We cannot read back the
	 * 'phy_addr_sel_strap' setting directly, so we need a test, which
	 * configuration is active:
	 * Special reg 18 of phy 3 reads as 0x0000, if 'phy_addr_sel_strap' is 0
	 * and the IDs are 0-1-2, else it contains something different from
	 * 0x0000, which means 'phy_addr_sel_strap' is 1 and the IDs are 1-2-3.
	 */
	reg = lan9303_port_phy_reg_read(chip, 3, MII_LAN911X_SPECIAL_MODES);
	if (reg < 0) {
		dev_err(chip->dev, "Failed to detect phy config: %d\n", reg);
		return reg;
	}

	if (reg != 0)
		chip->phy_addr_sel_strap = 1;
	else
		chip->phy_addr_sel_strap = 0;

	dev_dbg(chip->dev, "Phy setup '%s' detected\n",
		chip->phy_addr_sel_strap ? "1-2-3" : "0-1-2");

	return 0;
}

#define LAN9303_MAC_RX_CFG_OFFS (LAN9303_MAC_RX_CFG_0 - LAN9303_PORT_0_OFFSET)
#define LAN9303_MAC_TX_CFG_OFFS (LAN9303_MAC_TX_CFG_0 - LAN9303_PORT_0_OFFSET)

static int lan9303_disable_packet_processing(struct lan9303 *chip,
					     unsigned int port)
{
	int ret;

	/* disable RX, but keep register reset default values else */
	ret = lan9303_write_switch_reg(chip, LAN9303_MAC_RX_CFG_OFFS + port,
				       LAN9303_MAC_RX_CFG_X_REJECT_MAC_TYPES);
	if (ret)
		return ret;

	/* disable TX, but keep register reset default values else */
	return lan9303_write_switch_reg(chip, LAN9303_MAC_TX_CFG_OFFS + port,
				LAN9303_MAC_TX_CFG_X_TX_IFG_CONFIG_DEFAULT |
				LAN9303_MAC_TX_CFG_X_TX_PAD_ENABLE);
}

static int lan9303_enable_packet_processing(struct lan9303 *chip,
					    unsigned int port)
{
	int ret;

	/* enable RX and keep register reset default values else */
	ret = lan9303_write_switch_reg(chip, LAN9303_MAC_RX_CFG_OFFS + port,
				       LAN9303_MAC_RX_CFG_X_REJECT_MAC_TYPES |
				       LAN9303_MAC_RX_CFG_X_RX_ENABLE);
	if (ret)
		return ret;

	/* enable TX and keep register reset default values else */
	return lan9303_write_switch_reg(chip, LAN9303_MAC_TX_CFG_OFFS + port,
				LAN9303_MAC_TX_CFG_X_TX_IFG_CONFIG_DEFAULT |
				LAN9303_MAC_TX_CFG_X_TX_PAD_ENABLE |
				LAN9303_MAC_TX_CFG_X_TX_ENABLE);
}

/* We want a special working switch:
 * - do not forward packets between port 1 and 2
 * - forward everything from port 1 to port 0
 * - forward everything from port 2 to port 0
 * - forward special tagged packets from port 0 to port 1 *or* port 2
 */
static int lan9303_separate_ports(struct lan9303 *chip)
{
	int ret;

	ret = lan9303_write_switch_reg(chip, LAN9303_SWE_PORT_MIRROR,
				LAN9303_SWE_PORT_MIRROR_SNIFFER_PORT0 |
				LAN9303_SWE_PORT_MIRROR_MIRRORED_PORT1 |
				LAN9303_SWE_PORT_MIRROR_MIRRORED_PORT2 |
				LAN9303_SWE_PORT_MIRROR_ENABLE_RX_MIRRORING |
				LAN9303_SWE_PORT_MIRROR_SNIFF_ALL);
	if (ret)
		return ret;

	/* enable defining the destination port via special VLAN tagging
	 * for port 0
	 */
	ret = lan9303_write_switch_reg(chip, LAN9303_SWE_INGRESS_PORT_TYPE,
				       0x03);
	if (ret)
		return ret;

	/* tag incoming packets at port 1 and 2 on their way to port 0 to be
	 * able to discover their source port
	 */
	ret = lan9303_write_switch_reg(chip, LAN9303_BM_EGRSS_PORT_TYPE,
			LAN9303_BM_EGRSS_PORT_TYPE_SPECIAL_TAG_PORT0);
	if (ret)
		return ret;

	/* prevent port 1 and 2 from forwarding packets by their own */
	return lan9303_write_switch_reg(chip, LAN9303_SWE_PORT_STATE,
				LAN9303_SWE_PORT_STATE_FORWARDING_PORT0 |
				LAN9303_SWE_PORT_STATE_BLOCKING_PORT1 |
				LAN9303_SWE_PORT_STATE_BLOCKING_PORT2);
}

static int lan9303_handle_reset(struct lan9303 *chip)
{
	if (!chip->reset_gpio)
		return 0;

	if (chip->reset_duration != 0)
		msleep(chip->reset_duration);

	/* release (deassert) reset and activate the device */
	gpiod_set_value_cansleep(chip->reset_gpio, 0);

	return 0;
}

/* stop processing packets for all ports */
static int lan9303_disable_processing(struct lan9303 *chip)
{
	int ret;

	ret = lan9303_disable_packet_processing(chip, LAN9303_PORT_0_OFFSET);
	if (ret)
		return ret;
	ret = lan9303_disable_packet_processing(chip, LAN9303_PORT_1_OFFSET);
	if (ret)
		return ret;
	return lan9303_disable_packet_processing(chip, LAN9303_PORT_2_OFFSET);
}

static int lan9303_check_device(struct lan9303 *chip)
{
	int ret;
	u32 reg;

	ret = lan9303_read(chip->regmap, LAN9303_CHIP_REV, &reg);
	if (ret) {
		dev_err(chip->dev, "failed to read chip revision register: %d\n",
			ret);
		if (!chip->reset_gpio) {
			dev_dbg(chip->dev,
				"hint: maybe failed due to missing reset GPIO\n");
		}
		return ret;
	}

	if ((reg >> 16) != LAN9303_CHIP_ID) {
		dev_err(chip->dev, "expecting LAN9303 chip, but found: %X\n",
			reg >> 16);
		return ret;
	}

	/* The default state of the LAN9303 device is to forward packets between
	 * all ports (if not configured differently by an external EEPROM).
	 * The initial state of a DSA device must be forwarding packets only
	 * between the external and the internal ports and no forwarding
	 * between the external ports. In preparation we stop packet handling
	 * at all for now until the LAN9303 device is re-programmed accordingly.
	 */
	ret = lan9303_disable_processing(chip);
	if (ret)
		dev_warn(chip->dev, "failed to disable switching %d\n", ret);

	dev_info(chip->dev, "Found LAN9303 rev. %u\n", reg & 0xffff);

	ret = lan9303_detect_phy_setup(chip);
	if (ret) {
		dev_err(chip->dev,
			"failed to discover phy bootstrap setup: %d\n", ret);
		return ret;
	}

	return 0;
}

/* ---------------------------- DSA -----------------------------------*/

static enum dsa_tag_protocol lan9303_get_tag_protocol(struct dsa_switch *ds)
{
	return DSA_TAG_PROTO_LAN9303;
}

static int lan9303_setup(struct dsa_switch *ds)
{
	struct lan9303 *chip = ds->priv;
	int ret;

	/* Make sure that port 0 is the cpu port */
	if (!dsa_is_cpu_port(ds, 0)) {
		dev_err(chip->dev, "port 0 is not the CPU port\n");
		return -EINVAL;
	}

	ret = lan9303_separate_ports(chip);
	if (ret)
		dev_err(chip->dev, "failed to separate ports %d\n", ret);

	ret = lan9303_enable_packet_processing(chip, LAN9303_PORT_0_OFFSET);
	if (ret)
		dev_err(chip->dev, "failed to re-enable switching %d\n", ret);

	return 0;
}

struct lan9303_mib_desc {
	unsigned int offset; /* offset of first MAC */
	const char *name;
};

static const struct lan9303_mib_desc lan9303_mib[] = {
	{ .offset = LAN9303_MAC_RX_BRDCST_CNT_0, .name = "RxBroad", },
	{ .offset = LAN9303_MAC_RX_PAUSE_CNT_0, .name = "RxPause", },
	{ .offset = LAN9303_MAC_RX_MULCST_CNT_0, .name = "RxMulti", },
	{ .offset = LAN9303_MAC_RX_PKTOK_CNT_0, .name = "RxOk", },
	{ .offset = LAN9303_MAC_RX_CRCERR_CNT_0, .name = "RxCrcErr", },
	{ .offset = LAN9303_MAC_RX_ALIGN_CNT_0, .name = "RxAlignErr", },
	{ .offset = LAN9303_MAC_RX_JABB_CNT_0, .name = "RxJabber", },
	{ .offset = LAN9303_MAC_RX_FRAG_CNT_0, .name = "RxFragment", },
	{ .offset = LAN9303_MAC_RX_64_CNT_0, .name = "Rx64Byte", },
	{ .offset = LAN9303_MAC_RX_127_CNT_0, .name = "Rx128Byte", },
	{ .offset = LAN9303_MAC_RX_255_CNT_0, .name = "Rx256Byte", },
	{ .offset = LAN9303_MAC_RX_511_CNT_0, .name = "Rx512Byte", },
	{ .offset = LAN9303_MAC_RX_1023_CNT_0, .name = "Rx1024Byte", },
	{ .offset = LAN9303_MAC_RX_MAX_CNT_0, .name = "RxMaxByte", },
	{ .offset = LAN9303_MAC_RX_PKTLEN_CNT_0, .name = "RxByteCnt", },
	{ .offset = LAN9303_MAC_RX_SYMBL_CNT_0, .name = "RxSymbolCnt", },
	{ .offset = LAN9303_MAC_RX_CTLFRM_CNT_0, .name = "RxCfs", },
	{ .offset = LAN9303_MAC_RX_OVRSZE_CNT_0, .name = "RxOverFlow", },
	{ .offset = LAN9303_MAC_TX_UNDSZE_CNT_0, .name = "TxShort", },
	{ .offset = LAN9303_MAC_TX_BRDCST_CNT_0, .name = "TxBroad", },
	{ .offset = LAN9303_MAC_TX_PAUSE_CNT_0, .name = "TxPause", },
	{ .offset = LAN9303_MAC_TX_MULCST_CNT_0, .name = "TxMulti", },
	{ .offset = LAN9303_MAC_RX_UNDSZE_CNT_0, .name = "TxUnderRun", },
	{ .offset = LAN9303_MAC_TX_64_CNT_0, .name = "Tx64Byte", },
	{ .offset = LAN9303_MAC_TX_127_CNT_0, .name = "Tx128Byte", },
	{ .offset = LAN9303_MAC_TX_255_CNT_0, .name = "Tx256Byte", },
	{ .offset = LAN9303_MAC_TX_511_CNT_0, .name = "Tx512Byte", },
	{ .offset = LAN9303_MAC_TX_1023_CNT_0, .name = "Tx1024Byte", },
	{ .offset = LAN9303_MAC_TX_MAX_CNT_0, .name = "TxMaxByte", },
	{ .offset = LAN9303_MAC_TX_PKTLEN_CNT_0, .name = "TxByteCnt", },
	{ .offset = LAN9303_MAC_TX_PKTOK_CNT_0, .name = "TxOk", },
	{ .offset = LAN9303_MAC_TX_TOTALCOL_CNT_0, .name = "TxCollision", },
	{ .offset = LAN9303_MAC_TX_MULTICOL_CNT_0, .name = "TxMultiCol", },
	{ .offset = LAN9303_MAC_TX_SNGLECOL_CNT_0, .name = "TxSingleCol", },
	{ .offset = LAN9303_MAC_TX_EXCOL_CNT_0, .name = "TxExcCol", },
	{ .offset = LAN9303_MAC_TX_DEFER_CNT_0, .name = "TxDefer", },
	{ .offset = LAN9303_MAC_TX_LATECOL_0, .name = "TxLateCol", },
};

static void lan9303_get_strings(struct dsa_switch *ds, int port, uint8_t *data)
{
	unsigned int u;

	for (u = 0; u < ARRAY_SIZE(lan9303_mib); u++) {
		strncpy(data + u * ETH_GSTRING_LEN, lan9303_mib[u].name,
			ETH_GSTRING_LEN);
	}
}

static void lan9303_get_ethtool_stats(struct dsa_switch *ds, int port,
				      uint64_t *data)
{
	struct lan9303 *chip = ds->priv;
	u32 reg;
	unsigned int u, poff;
	int ret;

	poff = port * 0x400;

	for (u = 0; u < ARRAY_SIZE(lan9303_mib); u++) {
		ret = lan9303_read_switch_reg(chip,
					      lan9303_mib[u].offset + poff,
					      &reg);
		if (ret)
			dev_warn(chip->dev, "Reading status reg %u failed\n",
				 lan9303_mib[u].offset + poff);
		data[u] = reg;
	}
}

static int lan9303_get_sset_count(struct dsa_switch *ds)
{
	return ARRAY_SIZE(lan9303_mib);
}

static int lan9303_phy_read(struct dsa_switch *ds, int phy, int regnum)
{
	struct lan9303 *chip = ds->priv;
	int phy_base = chip->phy_addr_sel_strap;

	if (phy == phy_base)
		return lan9303_virt_phy_reg_read(chip, regnum);
	if (phy > phy_base + 2)
		return -ENODEV;

	return lan9303_port_phy_reg_read(chip, phy, regnum);
}

static int lan9303_phy_write(struct dsa_switch *ds, int phy, int regnum,
			     u16 val)
{
	struct lan9303 *chip = ds->priv;
	int phy_base = chip->phy_addr_sel_strap;

	if (phy == phy_base)
		return lan9303_virt_phy_reg_write(chip, regnum, val);
	if (phy > phy_base + 2)
		return -ENODEV;

	return lan9303_phy_reg_write(chip, phy, regnum, val);
}

static int lan9303_port_enable(struct dsa_switch *ds, int port,
			       struct phy_device *phy)
{
	struct lan9303 *chip = ds->priv;

	/* enable internal packet processing */
	switch (port) {
	case 1:
		return lan9303_enable_packet_processing(chip,
							LAN9303_PORT_1_OFFSET);
	case 2:
		return lan9303_enable_packet_processing(chip,
							LAN9303_PORT_2_OFFSET);
	default:
		dev_dbg(chip->dev,
			"Error: request to power up invalid port %d\n", port);
	}

	return -ENODEV;
}

static void lan9303_port_disable(struct dsa_switch *ds, int port,
				 struct phy_device *phy)
{
	struct lan9303 *chip = ds->priv;

	/* disable internal packet processing */
	switch (port) {
	case 1:
		lan9303_disable_packet_processing(chip, LAN9303_PORT_1_OFFSET);
		lan9303_phy_reg_write(chip, chip->phy_addr_sel_strap + 1,
				      MII_BMCR, BMCR_PDOWN);
		break;
	case 2:
		lan9303_disable_packet_processing(chip, LAN9303_PORT_2_OFFSET);
		lan9303_phy_reg_write(chip, chip->phy_addr_sel_strap + 2,
				      MII_BMCR, BMCR_PDOWN);
		break;
	default:
		dev_dbg(chip->dev,
			"Error: request to power down invalid port %d\n", port);
	}
}

static struct dsa_switch_ops lan9303_switch_ops = {
	.get_tag_protocol = lan9303_get_tag_protocol,
	.setup = lan9303_setup,
	.get_strings = lan9303_get_strings,
	.phy_read = lan9303_phy_read,
	.phy_write = lan9303_phy_write,
	.get_ethtool_stats = lan9303_get_ethtool_stats,
	.get_sset_count = lan9303_get_sset_count,
	.port_enable = lan9303_port_enable,
	.port_disable = lan9303_port_disable,
};

static int lan9303_register_switch(struct lan9303 *chip)
{
	chip->ds = dsa_switch_alloc(chip->dev, DSA_MAX_PORTS);
	if (!chip->ds)
		return -ENOMEM;

	chip->ds->priv = chip;
	chip->ds->ops = &lan9303_switch_ops;
	chip->ds->phys_mii_mask = chip->phy_addr_sel_strap ? 0xe : 0x7;

	return dsa_register_switch(chip->ds);
}

static void lan9303_probe_reset_gpio(struct lan9303 *chip,
				     struct device_node *np)
{
	chip->reset_gpio = devm_gpiod_get_optional(chip->dev, "reset",
						   GPIOD_OUT_LOW);

	if (!chip->reset_gpio) {
		dev_dbg(chip->dev, "No reset GPIO defined\n");
		return;
	}

	chip->reset_duration = 200;

	if (np) {
		of_property_read_u32(np, "reset-duration",
				     &chip->reset_duration);
	} else {
		dev_dbg(chip->dev, "reset duration defaults to 200 ms\n");
	}

	/* A sane reset duration should not be longer than 1s */
	if (chip->reset_duration > 1000)
		chip->reset_duration = 1000;
}

int lan9303_probe(struct lan9303 *chip, struct device_node *np)
{
	int ret;

	mutex_init(&chip->indirect_mutex);

	lan9303_probe_reset_gpio(chip, np);

	ret = lan9303_handle_reset(chip);
	if (ret)
		return ret;

	ret = lan9303_check_device(chip);
	if (ret)
		return ret;

	ret = lan9303_register_switch(chip);
	if (ret) {
		dev_dbg(chip->dev, "Failed to register switch: %d\n", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(lan9303_probe);

int lan9303_remove(struct lan9303 *chip)
{
	int rc;

	rc = lan9303_disable_processing(chip);
	if (rc != 0)
		dev_warn(chip->dev, "shutting down failed\n");

	dsa_unregister_switch(chip->ds);

	/* assert reset to the whole device to prevent it from doing anything */
	gpiod_set_value_cansleep(chip->reset_gpio, 1);
	gpiod_unexport(chip->reset_gpio);

	return 0;
}
EXPORT_SYMBOL(lan9303_remove);

MODULE_AUTHOR("Juergen Borleis <kernel@pengutronix.de>");
MODULE_DESCRIPTION("Core driver for SMSC/Microchip LAN9303 three port ethernet switch");
MODULE_LICENSE("GPL v2");
