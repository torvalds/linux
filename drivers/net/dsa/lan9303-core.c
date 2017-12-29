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
#include <linux/phy.h>
#include <linux/if_bridge.h>
#include <linux/etherdevice.h>

#include "lan9303.h"

#define LAN9303_NUM_PORTS 3

/* 13.2 System Control and Status Registers
 * Multiply register number by 4 to get address offset.
 */
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
#define  LAN9303_VIRT_SPECIAL_TURBO BIT(10) /*Turbo MII Enable*/

/*13.4 Switch Fabric Control and Status Registers
 * Accessed indirectly via SWITCH_CSR_CMD, SWITCH_CSR_DATA.
 */
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
# define LAN9303_ALR_CMD_MAKE_ENTRY    BIT(2)
# define LAN9303_ALR_CMD_GET_FIRST     BIT(1)
# define LAN9303_ALR_CMD_GET_NEXT      BIT(0)
#define LAN9303_SWE_ALR_WR_DAT_0 0x1801
#define LAN9303_SWE_ALR_WR_DAT_1 0x1802
# define LAN9303_ALR_DAT1_VALID        BIT(26)
# define LAN9303_ALR_DAT1_END_OF_TABL  BIT(25)
# define LAN9303_ALR_DAT1_AGE_OVERRID  BIT(25)
# define LAN9303_ALR_DAT1_STATIC       BIT(24)
# define LAN9303_ALR_DAT1_PORT_BITOFFS  16
# define LAN9303_ALR_DAT1_PORT_MASK    (7 << LAN9303_ALR_DAT1_PORT_BITOFFS)
#define LAN9303_SWE_ALR_RD_DAT_0 0x1805
#define LAN9303_SWE_ALR_RD_DAT_1 0x1806
#define LAN9303_SWE_ALR_CMD_STS 0x1808
# define ALR_STS_MAKE_PEND     BIT(0)
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
# define LAN9303_SWE_GLB_INGR_IGMP_TRAP BIT(7)
# define LAN9303_SWE_GLB_INGR_IGMP_PORT(p) BIT(10 + p)
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
# define LAN9303_SWE_PORT_STATE_DISABLED_PORT0 (3)
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
# define LAN9303_SWE_PORT_MIRROR_DISABLED 0
#define LAN9303_SWE_INGRESS_PORT_TYPE 0x1847
#define  LAN9303_SWE_INGRESS_PORT_TYPE_VLAN 3
#define LAN9303_BM_CFG 0x1c00
#define LAN9303_BM_EGRSS_PORT_TYPE 0x1c0c
# define LAN9303_BM_EGRSS_PORT_TYPE_SPECIAL_TAG_PORT2 (BIT(17) | BIT(16))
# define LAN9303_BM_EGRSS_PORT_TYPE_SPECIAL_TAG_PORT1 (BIT(9) | BIT(8))
# define LAN9303_BM_EGRSS_PORT_TYPE_SPECIAL_TAG_PORT0 (BIT(1) | BIT(0))

#define LAN9303_SWITCH_PORT_REG(port, reg0) (0x400 * (port) + (reg0))

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

static int lan9303_read_wait(struct lan9303 *chip, int offset, u32 mask)
{
	int i;

	for (i = 0; i < 25; i++) {
		u32 reg;
		int ret;

		ret = lan9303_read(chip->regmap, offset, &reg);
		if (ret) {
			dev_err(chip->dev, "%s failed to read offset %d: %d\n",
				__func__, offset, ret);
			return ret;
		}
		if (!(reg & mask))
			return 0;
		usleep_range(1000, 2000);
	}

	return -ETIMEDOUT;
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

static int lan9303_indirect_phy_wait_for_completion(struct lan9303 *chip)
{
	return lan9303_read_wait(chip, LAN9303_PMI_ACCESS,
				 LAN9303_PMI_ACCESS_MII_BUSY);
}

static int lan9303_indirect_phy_read(struct lan9303 *chip, int addr, int regnum)
{
	int ret;
	u32 val;

	val = LAN9303_PMI_ACCESS_PHY_ADDR(addr);
	val |= LAN9303_PMI_ACCESS_MIIRINDA(regnum);

	mutex_lock(&chip->indirect_mutex);

	ret = lan9303_indirect_phy_wait_for_completion(chip);
	if (ret)
		goto on_error;

	/* start the MII read cycle */
	ret = regmap_write(chip->regmap, LAN9303_PMI_ACCESS, val);
	if (ret)
		goto on_error;

	ret = lan9303_indirect_phy_wait_for_completion(chip);
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

static int lan9303_indirect_phy_write(struct lan9303 *chip, int addr,
				      int regnum, u16 val)
{
	int ret;
	u32 reg;

	reg = LAN9303_PMI_ACCESS_PHY_ADDR(addr);
	reg |= LAN9303_PMI_ACCESS_MIIRINDA(regnum);
	reg |= LAN9303_PMI_ACCESS_MII_WRITE;

	mutex_lock(&chip->indirect_mutex);

	ret = lan9303_indirect_phy_wait_for_completion(chip);
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

const struct lan9303_phy_ops lan9303_indirect_phy_ops = {
	.phy_read = lan9303_indirect_phy_read,
	.phy_write = lan9303_indirect_phy_write,
};
EXPORT_SYMBOL_GPL(lan9303_indirect_phy_ops);

static int lan9303_switch_wait_for_completion(struct lan9303 *chip)
{
	return lan9303_read_wait(chip, LAN9303_SWITCH_CSR_CMD,
				 LAN9303_SWITCH_CSR_CMD_BUSY);
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

static int lan9303_write_switch_reg_mask(struct lan9303 *chip, u16 regnum,
					 u32 val, u32 mask)
{
	int ret;
	u32 reg;

	ret = lan9303_read_switch_reg(chip, regnum, &reg);
	if (ret)
		return ret;

	reg = (reg & ~mask) | val;

	return lan9303_write_switch_reg(chip, regnum, reg);
}

static int lan9303_write_switch_port(struct lan9303 *chip, int port,
				     u16 regnum, u32 val)
{
	return lan9303_write_switch_reg(
		chip, LAN9303_SWITCH_PORT_REG(port, regnum), val);
}

static int lan9303_read_switch_port(struct lan9303 *chip, int port,
				    u16 regnum, u32 *val)
{
	return lan9303_read_switch_reg(
		chip, LAN9303_SWITCH_PORT_REG(port, regnum), val);
}

static int lan9303_detect_phy_setup(struct lan9303 *chip)
{
	int reg;

	/* Calculate chip->phy_addr_base:
	 * Depending on the 'phy_addr_sel_strap' setting, the three phys are
	 * using IDs 0-1-2 or IDs 1-2-3. We cannot read back the
	 * 'phy_addr_sel_strap' setting directly, so we need a test, which
	 * configuration is active:
	 * Special reg 18 of phy 3 reads as 0x0000, if 'phy_addr_sel_strap' is 0
	 * and the IDs are 0-1-2, else it contains something different from
	 * 0x0000, which means 'phy_addr_sel_strap' is 1 and the IDs are 1-2-3.
	 * 0xffff is returned on MDIO read with no response.
	 */
	reg = chip->ops->phy_read(chip, 3, MII_LAN911X_SPECIAL_MODES);
	if (reg < 0) {
		dev_err(chip->dev, "Failed to detect phy config: %d\n", reg);
		return reg;
	}

	if ((reg != 0) && (reg != 0xffff))
		chip->phy_addr_base = 1;
	else
		chip->phy_addr_base = 0;

	dev_dbg(chip->dev, "Phy setup '%s' detected\n",
		chip->phy_addr_base ? "1-2-3" : "0-1-2");

	return 0;
}

/* Map ALR-port bits to port bitmap, and back */
static const int alrport_2_portmap[] = {1, 2, 4, 0, 3, 5, 6, 7 };
static const int portmap_2_alrport[] = {3, 0, 1, 4, 2, 5, 6, 7 };

/* Return pointer to first free ALR cache entry, return NULL if none */
static struct lan9303_alr_cache_entry *
lan9303_alr_cache_find_free(struct lan9303 *chip)
{
	int i;
	struct lan9303_alr_cache_entry *entr = chip->alr_cache;

	for (i = 0; i < LAN9303_NUM_ALR_RECORDS; i++, entr++)
		if (entr->port_map == 0)
			return entr;

	return NULL;
}

/* Return pointer to ALR cache entry matching MAC address */
static struct lan9303_alr_cache_entry *
lan9303_alr_cache_find_mac(struct lan9303 *chip, const u8 *mac_addr)
{
	int i;
	struct lan9303_alr_cache_entry *entr = chip->alr_cache;

	BUILD_BUG_ON_MSG(sizeof(struct lan9303_alr_cache_entry) & 1,
			 "ether_addr_equal require u16 alignment");

	for (i = 0; i < LAN9303_NUM_ALR_RECORDS; i++, entr++)
		if (ether_addr_equal(entr->mac_addr, mac_addr))
			return entr;

	return NULL;
}

static int lan9303_csr_reg_wait(struct lan9303 *chip, int regno, u32 mask)
{
	int i;

	for (i = 0; i < 25; i++) {
		u32 reg;

		lan9303_read_switch_reg(chip, regno, &reg);
		if (!(reg & mask))
			return 0;
		usleep_range(1000, 2000);
	}

	return -ETIMEDOUT;
}

static int lan9303_alr_make_entry_raw(struct lan9303 *chip, u32 dat0, u32 dat1)
{
	lan9303_write_switch_reg(chip, LAN9303_SWE_ALR_WR_DAT_0, dat0);
	lan9303_write_switch_reg(chip, LAN9303_SWE_ALR_WR_DAT_1, dat1);
	lan9303_write_switch_reg(chip, LAN9303_SWE_ALR_CMD,
				 LAN9303_ALR_CMD_MAKE_ENTRY);
	lan9303_csr_reg_wait(chip, LAN9303_SWE_ALR_CMD_STS, ALR_STS_MAKE_PEND);
	lan9303_write_switch_reg(chip, LAN9303_SWE_ALR_CMD, 0);

	return 0;
}

typedef void alr_loop_cb_t(struct lan9303 *chip, u32 dat0, u32 dat1,
			   int portmap, void *ctx);

static void lan9303_alr_loop(struct lan9303 *chip, alr_loop_cb_t *cb, void *ctx)
{
	int i;

	mutex_lock(&chip->alr_mutex);
	lan9303_write_switch_reg(chip, LAN9303_SWE_ALR_CMD,
				 LAN9303_ALR_CMD_GET_FIRST);
	lan9303_write_switch_reg(chip, LAN9303_SWE_ALR_CMD, 0);

	for (i = 1; i < LAN9303_NUM_ALR_RECORDS; i++) {
		u32 dat0, dat1;
		int alrport, portmap;

		lan9303_read_switch_reg(chip, LAN9303_SWE_ALR_RD_DAT_0, &dat0);
		lan9303_read_switch_reg(chip, LAN9303_SWE_ALR_RD_DAT_1, &dat1);
		if (dat1 & LAN9303_ALR_DAT1_END_OF_TABL)
			break;

		alrport = (dat1 & LAN9303_ALR_DAT1_PORT_MASK) >>
						LAN9303_ALR_DAT1_PORT_BITOFFS;
		portmap = alrport_2_portmap[alrport];

		cb(chip, dat0, dat1, portmap, ctx);

		lan9303_write_switch_reg(chip, LAN9303_SWE_ALR_CMD,
					 LAN9303_ALR_CMD_GET_NEXT);
		lan9303_write_switch_reg(chip, LAN9303_SWE_ALR_CMD, 0);
	}
	mutex_unlock(&chip->alr_mutex);
}

static void alr_reg_to_mac(u32 dat0, u32 dat1, u8 mac[6])
{
	mac[0] = (dat0 >>  0) & 0xff;
	mac[1] = (dat0 >>  8) & 0xff;
	mac[2] = (dat0 >> 16) & 0xff;
	mac[3] = (dat0 >> 24) & 0xff;
	mac[4] = (dat1 >>  0) & 0xff;
	mac[5] = (dat1 >>  8) & 0xff;
}

struct del_port_learned_ctx {
	int port;
};

/* Clear learned (non-static) entry on given port */
static void alr_loop_cb_del_port_learned(struct lan9303 *chip, u32 dat0,
					 u32 dat1, int portmap, void *ctx)
{
	struct del_port_learned_ctx *del_ctx = ctx;
	int port = del_ctx->port;

	if (((BIT(port) & portmap) == 0) || (dat1 & LAN9303_ALR_DAT1_STATIC))
		return;

	/* learned entries has only one port, we can just delete */
	dat1 &= ~LAN9303_ALR_DAT1_VALID; /* delete entry */
	lan9303_alr_make_entry_raw(chip, dat0, dat1);
}

struct port_fdb_dump_ctx {
	int port;
	void *data;
	dsa_fdb_dump_cb_t *cb;
};

static void alr_loop_cb_fdb_port_dump(struct lan9303 *chip, u32 dat0,
				      u32 dat1, int portmap, void *ctx)
{
	struct port_fdb_dump_ctx *dump_ctx = ctx;
	u8 mac[ETH_ALEN];
	bool is_static;

	if ((BIT(dump_ctx->port) & portmap) == 0)
		return;

	alr_reg_to_mac(dat0, dat1, mac);
	is_static = !!(dat1 & LAN9303_ALR_DAT1_STATIC);
	dump_ctx->cb(mac, 0, is_static, dump_ctx->data);
}

/* Set a static ALR entry. Delete entry if port_map is zero */
static void lan9303_alr_set_entry(struct lan9303 *chip, const u8 *mac,
				  u8 port_map, bool stp_override)
{
	u32 dat0, dat1, alr_port;

	dev_dbg(chip->dev, "%s(%pM, %d)\n", __func__, mac, port_map);
	dat1 = LAN9303_ALR_DAT1_STATIC;
	if (port_map)
		dat1 |= LAN9303_ALR_DAT1_VALID;
	/* otherwise no ports: delete entry */
	if (stp_override)
		dat1 |= LAN9303_ALR_DAT1_AGE_OVERRID;

	alr_port = portmap_2_alrport[port_map & 7];
	dat1 &= ~LAN9303_ALR_DAT1_PORT_MASK;
	dat1 |= alr_port << LAN9303_ALR_DAT1_PORT_BITOFFS;

	dat0 = 0;
	dat0 |= (mac[0] << 0);
	dat0 |= (mac[1] << 8);
	dat0 |= (mac[2] << 16);
	dat0 |= (mac[3] << 24);

	dat1 |= (mac[4] << 0);
	dat1 |= (mac[5] << 8);

	lan9303_alr_make_entry_raw(chip, dat0, dat1);
}

/* Add port to static ALR entry, create new static entry if needed */
static int lan9303_alr_add_port(struct lan9303 *chip, const u8 *mac, int port,
				bool stp_override)
{
	struct lan9303_alr_cache_entry *entr;

	mutex_lock(&chip->alr_mutex);
	entr = lan9303_alr_cache_find_mac(chip, mac);
	if (!entr) { /*New entry */
		entr = lan9303_alr_cache_find_free(chip);
		if (!entr) {
			mutex_unlock(&chip->alr_mutex);
			return -ENOSPC;
		}
		ether_addr_copy(entr->mac_addr, mac);
	}
	entr->port_map |= BIT(port);
	entr->stp_override = stp_override;
	lan9303_alr_set_entry(chip, mac, entr->port_map, stp_override);
	mutex_unlock(&chip->alr_mutex);

	return 0;
}

/* Delete static port from ALR entry, delete entry if last port */
static int lan9303_alr_del_port(struct lan9303 *chip, const u8 *mac, int port)
{
	struct lan9303_alr_cache_entry *entr;

	mutex_lock(&chip->alr_mutex);
	entr = lan9303_alr_cache_find_mac(chip, mac);
	if (!entr)
		goto out;  /* no static entry found */

	entr->port_map &= ~BIT(port);
	if (entr->port_map == 0) /* zero means its free again */
		eth_zero_addr(entr->mac_addr);
	lan9303_alr_set_entry(chip, mac, entr->port_map, entr->stp_override);

out:
	mutex_unlock(&chip->alr_mutex);
	return 0;
}

static int lan9303_disable_processing_port(struct lan9303 *chip,
					   unsigned int port)
{
	int ret;

	/* disable RX, but keep register reset default values else */
	ret = lan9303_write_switch_port(chip, port, LAN9303_MAC_RX_CFG_0,
					LAN9303_MAC_RX_CFG_X_REJECT_MAC_TYPES);
	if (ret)
		return ret;

	/* disable TX, but keep register reset default values else */
	return lan9303_write_switch_port(chip, port, LAN9303_MAC_TX_CFG_0,
				LAN9303_MAC_TX_CFG_X_TX_IFG_CONFIG_DEFAULT |
				LAN9303_MAC_TX_CFG_X_TX_PAD_ENABLE);
}

static int lan9303_enable_processing_port(struct lan9303 *chip,
					  unsigned int port)
{
	int ret;

	/* enable RX and keep register reset default values else */
	ret = lan9303_write_switch_port(chip, port, LAN9303_MAC_RX_CFG_0,
					LAN9303_MAC_RX_CFG_X_REJECT_MAC_TYPES |
					LAN9303_MAC_RX_CFG_X_RX_ENABLE);
	if (ret)
		return ret;

	/* enable TX and keep register reset default values else */
	return lan9303_write_switch_port(chip, port, LAN9303_MAC_TX_CFG_0,
				LAN9303_MAC_TX_CFG_X_TX_IFG_CONFIG_DEFAULT |
				LAN9303_MAC_TX_CFG_X_TX_PAD_ENABLE |
				LAN9303_MAC_TX_CFG_X_TX_ENABLE);
}

/* forward special tagged packets from port 0 to port 1 *or* port 2 */
static int lan9303_setup_tagging(struct lan9303 *chip)
{
	int ret;
	u32 val;
	/* enable defining the destination port via special VLAN tagging
	 * for port 0
	 */
	ret = lan9303_write_switch_reg(chip, LAN9303_SWE_INGRESS_PORT_TYPE,
				       LAN9303_SWE_INGRESS_PORT_TYPE_VLAN);
	if (ret)
		return ret;

	/* tag incoming packets at port 1 and 2 on their way to port 0 to be
	 * able to discover their source port
	 */
	val = LAN9303_BM_EGRSS_PORT_TYPE_SPECIAL_TAG_PORT0;
	return lan9303_write_switch_reg(chip, LAN9303_BM_EGRSS_PORT_TYPE, val);
}

/* We want a special working switch:
 * - do not forward packets between port 1 and 2
 * - forward everything from port 1 to port 0
 * - forward everything from port 2 to port 0
 */
static int lan9303_separate_ports(struct lan9303 *chip)
{
	int ret;

	lan9303_alr_del_port(chip, eth_stp_addr, 0);
	ret = lan9303_write_switch_reg(chip, LAN9303_SWE_PORT_MIRROR,
				LAN9303_SWE_PORT_MIRROR_SNIFFER_PORT0 |
				LAN9303_SWE_PORT_MIRROR_MIRRORED_PORT1 |
				LAN9303_SWE_PORT_MIRROR_MIRRORED_PORT2 |
				LAN9303_SWE_PORT_MIRROR_ENABLE_RX_MIRRORING |
				LAN9303_SWE_PORT_MIRROR_SNIFF_ALL);
	if (ret)
		return ret;

	/* prevent port 1 and 2 from forwarding packets by their own */
	return lan9303_write_switch_reg(chip, LAN9303_SWE_PORT_STATE,
				LAN9303_SWE_PORT_STATE_FORWARDING_PORT0 |
				LAN9303_SWE_PORT_STATE_BLOCKING_PORT1 |
				LAN9303_SWE_PORT_STATE_BLOCKING_PORT2);
}

static void lan9303_bridge_ports(struct lan9303 *chip)
{
	/* ports bridged: remove mirroring */
	lan9303_write_switch_reg(chip, LAN9303_SWE_PORT_MIRROR,
				 LAN9303_SWE_PORT_MIRROR_DISABLED);

	lan9303_write_switch_reg(chip, LAN9303_SWE_PORT_STATE,
				 chip->swe_port_state);
	lan9303_alr_add_port(chip, eth_stp_addr, 0, true);
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
	int p;

	for (p = 1; p < LAN9303_NUM_PORTS; p++) {
		int ret = lan9303_disable_processing_port(chip, p);

		if (ret)
			return ret;
	}

	return 0;
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

static enum dsa_tag_protocol lan9303_get_tag_protocol(struct dsa_switch *ds,
						      int port)
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

	ret = lan9303_setup_tagging(chip);
	if (ret)
		dev_err(chip->dev, "failed to setup port tagging %d\n", ret);

	ret = lan9303_separate_ports(chip);
	if (ret)
		dev_err(chip->dev, "failed to separate ports %d\n", ret);

	ret = lan9303_enable_processing_port(chip, 0);
	if (ret)
		dev_err(chip->dev, "failed to re-enable switching %d\n", ret);

	/* Trap IGMP to port 0 */
	ret = lan9303_write_switch_reg_mask(chip, LAN9303_SWE_GLB_INGRESS_CFG,
					    LAN9303_SWE_GLB_INGR_IGMP_TRAP |
					    LAN9303_SWE_GLB_INGR_IGMP_PORT(0),
					    LAN9303_SWE_GLB_INGR_IGMP_PORT(1) |
					    LAN9303_SWE_GLB_INGR_IGMP_PORT(2));
	if (ret)
		dev_err(chip->dev, "failed to setup IGMP trap %d\n", ret);

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
	unsigned int u;

	for (u = 0; u < ARRAY_SIZE(lan9303_mib); u++) {
		u32 reg;
		int ret;

		ret = lan9303_read_switch_port(
			chip, port, lan9303_mib[u].offset, &reg);

		if (ret)
			dev_warn(chip->dev, "Reading status port %d reg %u failed\n",
				 port, lan9303_mib[u].offset);
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
	int phy_base = chip->phy_addr_base;

	if (phy == phy_base)
		return lan9303_virt_phy_reg_read(chip, regnum);
	if (phy > phy_base + 2)
		return -ENODEV;

	return chip->ops->phy_read(chip, phy, regnum);
}

static int lan9303_phy_write(struct dsa_switch *ds, int phy, int regnum,
			     u16 val)
{
	struct lan9303 *chip = ds->priv;
	int phy_base = chip->phy_addr_base;

	if (phy == phy_base)
		return lan9303_virt_phy_reg_write(chip, regnum, val);
	if (phy > phy_base + 2)
		return -ENODEV;

	return chip->ops->phy_write(chip, phy, regnum, val);
}

static void lan9303_adjust_link(struct dsa_switch *ds, int port,
				struct phy_device *phydev)
{
	struct lan9303 *chip = ds->priv;
	int ctl, res;

	if (!phy_is_pseudo_fixed_link(phydev))
		return;

	ctl = lan9303_phy_read(ds, port, MII_BMCR);

	ctl &= ~BMCR_ANENABLE;

	if (phydev->speed == SPEED_100)
		ctl |= BMCR_SPEED100;
	else if (phydev->speed == SPEED_10)
		ctl &= ~BMCR_SPEED100;
	else
		dev_err(ds->dev, "unsupported speed: %d\n", phydev->speed);

	if (phydev->duplex == DUPLEX_FULL)
		ctl |= BMCR_FULLDPLX;
	else
		ctl &= ~BMCR_FULLDPLX;

	res =  lan9303_phy_write(ds, port, MII_BMCR, ctl);

	if (port == chip->phy_addr_base) {
		/* Virtual Phy: Remove Turbo 200Mbit mode */
		lan9303_read(chip->regmap, LAN9303_VIRT_SPECIAL_CTRL, &ctl);

		ctl &= ~LAN9303_VIRT_SPECIAL_TURBO;
		res =  regmap_write(chip->regmap,
				    LAN9303_VIRT_SPECIAL_CTRL, ctl);
	}
}

static int lan9303_port_enable(struct dsa_switch *ds, int port,
			       struct phy_device *phy)
{
	struct lan9303 *chip = ds->priv;

	return lan9303_enable_processing_port(chip, port);
}

static void lan9303_port_disable(struct dsa_switch *ds, int port,
				 struct phy_device *phy)
{
	struct lan9303 *chip = ds->priv;

	lan9303_disable_processing_port(chip, port);
	lan9303_phy_write(ds, chip->phy_addr_base + port, MII_BMCR, BMCR_PDOWN);
}

static int lan9303_port_bridge_join(struct dsa_switch *ds, int port,
				    struct net_device *br)
{
	struct lan9303 *chip = ds->priv;

	dev_dbg(chip->dev, "%s(port %d)\n", __func__, port);
	if (dsa_to_port(ds, 1)->bridge_dev == dsa_to_port(ds, 2)->bridge_dev) {
		lan9303_bridge_ports(chip);
		chip->is_bridged = true;  /* unleash stp_state_set() */
	}

	return 0;
}

static void lan9303_port_bridge_leave(struct dsa_switch *ds, int port,
				      struct net_device *br)
{
	struct lan9303 *chip = ds->priv;

	dev_dbg(chip->dev, "%s(port %d)\n", __func__, port);
	if (chip->is_bridged) {
		lan9303_separate_ports(chip);
		chip->is_bridged = false;
	}
}

static void lan9303_port_stp_state_set(struct dsa_switch *ds, int port,
				       u8 state)
{
	int portmask, portstate;
	struct lan9303 *chip = ds->priv;

	dev_dbg(chip->dev, "%s(port %d, state %d)\n",
		__func__, port, state);

	switch (state) {
	case BR_STATE_DISABLED:
		portstate = LAN9303_SWE_PORT_STATE_DISABLED_PORT0;
		break;
	case BR_STATE_BLOCKING:
	case BR_STATE_LISTENING:
		portstate = LAN9303_SWE_PORT_STATE_BLOCKING_PORT0;
		break;
	case BR_STATE_LEARNING:
		portstate = LAN9303_SWE_PORT_STATE_LEARNING_PORT0;
		break;
	case BR_STATE_FORWARDING:
		portstate = LAN9303_SWE_PORT_STATE_FORWARDING_PORT0;
		break;
	default:
		portstate = LAN9303_SWE_PORT_STATE_DISABLED_PORT0;
		dev_err(chip->dev, "unknown stp state: port %d, state %d\n",
			port, state);
	}

	portmask = 0x3 << (port * 2);
	portstate <<= (port * 2);

	chip->swe_port_state = (chip->swe_port_state & ~portmask) | portstate;

	if (chip->is_bridged)
		lan9303_write_switch_reg(chip, LAN9303_SWE_PORT_STATE,
					 chip->swe_port_state);
	/* else: touching SWE_PORT_STATE would break port separation */
}

static void lan9303_port_fast_age(struct dsa_switch *ds, int port)
{
	struct lan9303 *chip = ds->priv;
	struct del_port_learned_ctx del_ctx = {
		.port = port,
	};

	dev_dbg(chip->dev, "%s(%d)\n", __func__, port);
	lan9303_alr_loop(chip, alr_loop_cb_del_port_learned, &del_ctx);
}

static int lan9303_port_fdb_add(struct dsa_switch *ds, int port,
				const unsigned char *addr, u16 vid)
{
	struct lan9303 *chip = ds->priv;

	dev_dbg(chip->dev, "%s(%d, %pM, %d)\n", __func__, port, addr, vid);
	if (vid)
		return -EOPNOTSUPP;

	return lan9303_alr_add_port(chip, addr, port, false);
}

static int lan9303_port_fdb_del(struct dsa_switch *ds, int port,
				const unsigned char *addr, u16 vid)

{
	struct lan9303 *chip = ds->priv;

	dev_dbg(chip->dev, "%s(%d, %pM, %d)\n", __func__, port, addr, vid);
	if (vid)
		return -EOPNOTSUPP;
	lan9303_alr_del_port(chip, addr, port);

	return 0;
}

static int lan9303_port_fdb_dump(struct dsa_switch *ds, int port,
				 dsa_fdb_dump_cb_t *cb, void *data)
{
	struct lan9303 *chip = ds->priv;
	struct port_fdb_dump_ctx dump_ctx = {
		.port = port,
		.data = data,
		.cb   = cb,
	};

	dev_dbg(chip->dev, "%s(%d)\n", __func__, port);
	lan9303_alr_loop(chip, alr_loop_cb_fdb_port_dump, &dump_ctx);

	return 0;
}

static int lan9303_port_mdb_prepare(struct dsa_switch *ds, int port,
				    const struct switchdev_obj_port_mdb *mdb)
{
	struct lan9303 *chip = ds->priv;

	dev_dbg(chip->dev, "%s(%d, %pM, %d)\n", __func__, port, mdb->addr,
		mdb->vid);
	if (mdb->vid)
		return -EOPNOTSUPP;
	if (lan9303_alr_cache_find_mac(chip, mdb->addr))
		return 0;
	if (!lan9303_alr_cache_find_free(chip))
		return -ENOSPC;

	return 0;
}

static void lan9303_port_mdb_add(struct dsa_switch *ds, int port,
				 const struct switchdev_obj_port_mdb *mdb)
{
	struct lan9303 *chip = ds->priv;

	dev_dbg(chip->dev, "%s(%d, %pM, %d)\n", __func__, port, mdb->addr,
		mdb->vid);
	lan9303_alr_add_port(chip, mdb->addr, port, false);
}

static int lan9303_port_mdb_del(struct dsa_switch *ds, int port,
				const struct switchdev_obj_port_mdb *mdb)
{
	struct lan9303 *chip = ds->priv;

	dev_dbg(chip->dev, "%s(%d, %pM, %d)\n", __func__, port, mdb->addr,
		mdb->vid);
	if (mdb->vid)
		return -EOPNOTSUPP;
	lan9303_alr_del_port(chip, mdb->addr, port);

	return 0;
}

static const struct dsa_switch_ops lan9303_switch_ops = {
	.get_tag_protocol = lan9303_get_tag_protocol,
	.setup = lan9303_setup,
	.get_strings = lan9303_get_strings,
	.phy_read = lan9303_phy_read,
	.phy_write = lan9303_phy_write,
	.adjust_link = lan9303_adjust_link,
	.get_ethtool_stats = lan9303_get_ethtool_stats,
	.get_sset_count = lan9303_get_sset_count,
	.port_enable = lan9303_port_enable,
	.port_disable = lan9303_port_disable,
	.port_bridge_join       = lan9303_port_bridge_join,
	.port_bridge_leave      = lan9303_port_bridge_leave,
	.port_stp_state_set     = lan9303_port_stp_state_set,
	.port_fast_age          = lan9303_port_fast_age,
	.port_fdb_add           = lan9303_port_fdb_add,
	.port_fdb_del           = lan9303_port_fdb_del,
	.port_fdb_dump          = lan9303_port_fdb_dump,
	.port_mdb_prepare       = lan9303_port_mdb_prepare,
	.port_mdb_add           = lan9303_port_mdb_add,
	.port_mdb_del           = lan9303_port_mdb_del,
};

static int lan9303_register_switch(struct lan9303 *chip)
{
	chip->ds = dsa_switch_alloc(chip->dev, LAN9303_NUM_PORTS);
	if (!chip->ds)
		return -ENOMEM;

	chip->ds->priv = chip;
	chip->ds->ops = &lan9303_switch_ops;
	chip->ds->phys_mii_mask = chip->phy_addr_base ? 0xe : 0x7;

	return dsa_register_switch(chip->ds);
}

static void lan9303_probe_reset_gpio(struct lan9303 *chip,
				     struct device_node *np)
{
	chip->reset_gpio = devm_gpiod_get_optional(chip->dev, "reset",
						   GPIOD_OUT_LOW);

	if (IS_ERR(chip->reset_gpio)) {
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
	mutex_init(&chip->alr_mutex);

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
